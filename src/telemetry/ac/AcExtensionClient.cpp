#include "telemetry/ac/AcExtensionClient.h"
#include "telemetry/common/WindowsSharedMemory.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUdpSocket>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace raceengineer {

#pragma pack(push, 1)
struct AcExtHeader {
    char magic[4];          // "RAEX"
    uint32_t version;       // 1
    uint32_t sequence;      // Seqlock counter: odd during write, even when stable
    int64_t timestampMs;    // Unix millisecond timestamp
    int32_t numCars;        // Number of records: player id 0 plus opponents (0..64)
    float gapAhead;         // Seconds, < 0 if none
    float gapBehind;        // Seconds, < 0 if none
    float playerSectors[3]; // Sector times, <= 0 if none
    float brakeTemps[4];    // FL, FR, RL, RR
    char oppAhead[64];      // UTF-8 driver ahead name
    char oppBehind[64];     // UTF-8 driver behind name
};

struct AcExtCar {
    int32_t carId;
    int32_t position;
    float speedKmh;
    float lastLap;
    float bestLap;
    float worldPos[3];      // x, y, z
    char driver[64];
    char car[64];
};

struct AcExtSharedData {
    AcExtHeader header;
    AcExtCar cars[64];
};
#pragma pack(pop)

static_assert(sizeof(AcExtHeader) == 188, "AcExtHeader size mismatch");
static_assert(sizeof(AcExtCar) == 160, "AcExtCar size mismatch");
static_assert(sizeof(AcExtSharedData) == 188 + 64 * 160, "AcExtSharedData size mismatch");

namespace {

std::string safeString(const char* buffer, size_t maxLen)
{
    size_t len = 0;
    while (len < maxLen && buffer[len] != '\0') {
        ++len;
    }
    return std::string(buffer, len);
}

} // namespace

struct AcExtensionClient::Impl final {
    std::unique_ptr<QUdpSocket> socket;
    quint16 port{9996};
    WindowsSharedMemory shm;
    std::chrono::steady_clock::time_point lastPacketTime{};
    std::chrono::steady_clock::time_point lastShmAttempt{};
    uint32_t lastReadSequence{0};
};

AcExtensionClient::AcExtensionClient()
    : impl_(std::make_unique<Impl>())
{
}

AcExtensionClient::~AcExtensionClient()
{
    stop();
}

bool AcExtensionClient::start(const uint16_t port)
{
    stop();
    impl_->port = port;
    impl_->shm.createOrOpen(L"Local\\race_engineer_ac_ext", sizeof(AcExtSharedData));

    impl_->socket = std::make_unique<QUdpSocket>();
    if (!impl_->socket->bind(QHostAddress::LocalHost, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        impl_->socket.reset();
    }
    return true;
}

void AcExtensionClient::stop() noexcept
{
    if (impl_->socket) {
        impl_->socket->close();
        impl_->socket.reset();
    }
    impl_->shm.close();
    opponents_.clear();
    playerPosition_.reset();
    gapAhead_.reset();
    gapBehind_.reset();
    opponentAhead_.reset();
    opponentBehind_.reset();
    playerSectors_.reset();
    brakeTemps_.reset();
    impl_->lastPacketTime = {};
    impl_->lastShmAttempt = {};
    impl_->lastReadSequence = 0;
}

bool AcExtensionClient::hasData() const noexcept
{
    if (impl_->lastPacketTime == std::chrono::steady_clock::time_point{}) {
        return false;
    }
    return std::chrono::steady_clock::now() - impl_->lastPacketTime < std::chrono::seconds(2);
}

void AcExtensionClient::update()
{
    const auto now = std::chrono::steady_clock::now();
    bool packetProcessed = false;

    // 1. Check Windows Shared Memory from Assetto Corsa Python companion app
    if (!impl_->shm.isOpen()) {
        if (now - impl_->lastShmAttempt > std::chrono::seconds(1)) {
            impl_->lastShmAttempt = now;
            impl_->shm.createOrOpen(L"Local\\race_engineer_ac_ext", sizeof(AcExtSharedData));
        }
    }

    if (impl_->shm.isOpen() && impl_->shm.size() >= sizeof(AcExtSharedData)) {
        const auto* src = static_cast<const AcExtSharedData*>(impl_->shm.data());
        if (std::memcmp(src->header.magic, "RAEX", 4) == 0 && src->header.version == 1) {
            AcExtSharedData snapshot{};
            for (int attempt = 0; attempt < 3; ++attempt) {
                const uint32_t seq1 = src->header.sequence;
                if (seq1 % 2 != 0 || seq1 == 0) {
                    continue; // Write in progress
                }
                std::memcpy(&snapshot, src, sizeof(AcExtSharedData));
                const uint32_t seq2 = src->header.sequence;
                if (seq1 == seq2) {
                    // Valid stable frame
                    if (seq1 != impl_->lastReadSequence) {
                        impl_->lastReadSequence = seq1;
                        impl_->lastPacketTime = now;
                    }
                    packetProcessed = true;

                    const int numCars = std::clamp(snapshot.header.numCars, 0, 64);
                    std::vector<OpponentState> parsedOpponents;
                    parsedOpponents.reserve(numCars);
                    playerPosition_.reset();

                    for (int i = 0; i < numCars; ++i) {
                        const auto& car = snapshot.cars[i];
                        if (car.carId == 0) {
                            if (car.position > 0) playerPosition_ = car.position;
                            continue;
                        }
                        OpponentState opp;
                        opp.carId = car.carId;
                        opp.position = car.position;
                        opp.speedKmh = car.speedKmh;
                        if (car.lastLap > 0.0f) {
                            opp.previousLapTimeSeconds = car.lastLap;
                        }
                        if (car.bestLap > 0.0f) {
                            opp.bestLapTimeSeconds = car.bestLap;
                        }
                        opp.worldPosition = std::array<double, 3>{
                            car.worldPos[0],
                            car.worldPos[1],
                            car.worldPos[2]
                        };
                        opp.driverName = safeString(car.driver, sizeof(car.driver));
                        opp.teamName = safeString(car.car, sizeof(car.car));

                        parsedOpponents.push_back(std::move(opp));
                    }
                    opponents_ = std::move(parsedOpponents);

                    if (snapshot.header.gapAhead >= 0.0f) {
                        gapAhead_ = snapshot.header.gapAhead;
                    } else {
                        gapAhead_.reset();
                    }

                    if (snapshot.header.gapBehind >= 0.0f) {
                        gapBehind_ = snapshot.header.gapBehind;
                    } else {
                        gapBehind_.reset();
                    }

                    const auto oppAhead = safeString(snapshot.header.oppAhead, sizeof(snapshot.header.oppAhead));
                    if (!oppAhead.empty()) {
                        opponentAhead_ = oppAhead;
                    } else {
                        opponentAhead_.reset();
                    }

                    const auto oppBehind = safeString(snapshot.header.oppBehind, sizeof(snapshot.header.oppBehind));
                    if (!oppBehind.empty()) {
                        opponentBehind_ = oppBehind;
                    } else {
                        opponentBehind_.reset();
                    }

                    if (snapshot.header.playerSectors[0] > 0.0f ||
                        snapshot.header.playerSectors[1] > 0.0f ||
                        snapshot.header.playerSectors[2] > 0.0f) {
                        playerSectors_ = std::array<double, 3>{
                            snapshot.header.playerSectors[0],
                            snapshot.header.playerSectors[1],
                            snapshot.header.playerSectors[2]
                        };
                    }

                    if (snapshot.header.brakeTemps[0] > 0.0f ||
                        snapshot.header.brakeTemps[1] > 0.0f) {
                        brakeTemps_ = WheelValues{
                            snapshot.header.brakeTemps[0],
                            snapshot.header.brakeTemps[1],
                            snapshot.header.brakeTemps[2],
                            snapshot.header.brakeTemps[3]
                        };
                    }

                    break;
                }
            }
        }
    }

    // 2. Check UDP datagrams as secondary / fallback transport
    if (impl_->socket) {
        while (impl_->socket->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(static_cast<qsizetype>(impl_->socket->pendingDatagramSize()));
            if (impl_->socket->readDatagram(datagram.data(), datagram.size()) <= 0) continue;

            const auto doc = QJsonDocument::fromJson(datagram);
            if (!doc.isObject()) continue;
            const auto root = doc.object();

            packetProcessed = true;
            impl_->lastPacketTime = now;

            // Parse opponents
            if (root.contains(QStringLiteral("cars"))) {
                const auto carsArray = root.value(QStringLiteral("cars")).toArray();
                std::vector<OpponentState> parsedOpponents;
                parsedOpponents.reserve(carsArray.size());

                for (const auto& carVal : carsArray) {
                    if (!carVal.isObject()) continue;
                    const auto carObj = carVal.toObject();

                    OpponentState opp;
                    opp.carId = carObj.value(QStringLiteral("id")).toInt(-1);
                    if (opp.carId == 0) {
                        const int position = carObj.value(QStringLiteral("pos")).toInt();
                        if (position > 0) playerPosition_ = position;
                        continue;
                    }
                    opp.driverName = carObj.value(QStringLiteral("driver")).toString().toStdString();
                    opp.teamName = carObj.value(QStringLiteral("car")).toString().toStdString();
                    if (carObj.contains(QStringLiteral("pos"))) {
                        opp.position = carObj.value(QStringLiteral("pos")).toInt();
                    }
                    if (carObj.contains(QStringLiteral("speed"))) {
                        opp.speedKmh = carObj.value(QStringLiteral("speed")).toDouble();
                    }
                    if (carObj.contains(QStringLiteral("last_lap"))) {
                        opp.previousLapTimeSeconds = carObj.value(QStringLiteral("last_lap")).toDouble();
                    }
                    if (carObj.contains(QStringLiteral("best_lap"))) {
                        opp.bestLapTimeSeconds = carObj.value(QStringLiteral("best_lap")).toDouble();
                    }
                    if (carObj.contains(QStringLiteral("coords"))) {
                        const auto coordsArr = carObj.value(QStringLiteral("coords")).toArray();
                        if (coordsArr.size() >= 3) {
                            opp.worldPosition = std::array<double, 3>{
                                coordsArr.at(0).toDouble(),
                                coordsArr.at(1).toDouble(),
                                coordsArr.at(2).toDouble()
                            };
                        }
                    }
                    parsedOpponents.push_back(std::move(opp));
                }
                opponents_ = std::move(parsedOpponents);
            }

            // Parse player extras (gaps, opponent names, sectors, brake temps)
            if (root.contains(QStringLiteral("player"))) {
                const auto playerObj = root.value(QStringLiteral("player")).toObject();
                if (playerObj.contains(QStringLiteral("position"))) {
                    const int position = playerObj.value(QStringLiteral("position")).toInt();
                    if (position > 0) playerPosition_ = position;
                    else playerPosition_.reset();
                }
                if (playerObj.contains(QStringLiteral("gap_ahead"))) {
                    gapAhead_ = playerObj.value(QStringLiteral("gap_ahead")).toDouble();
                }
                if (playerObj.contains(QStringLiteral("gap_behind"))) {
                    gapBehind_ = playerObj.value(QStringLiteral("gap_behind")).toDouble();
                }
                if (playerObj.contains(QStringLiteral("opp_ahead"))) {
                    const auto name = playerObj.value(QStringLiteral("opp_ahead")).toString().trimmed();
                    if (!name.isEmpty()) opponentAhead_ = name.toStdString();
                }
                if (playerObj.contains(QStringLiteral("opp_behind"))) {
                    const auto name = playerObj.value(QStringLiteral("opp_behind")).toString().trimmed();
                    if (!name.isEmpty()) opponentBehind_ = name.toStdString();
                }
                if (playerObj.contains(QStringLiteral("sectors"))) {
                    const auto sectorsArr = playerObj.value(QStringLiteral("sectors")).toArray();
                    if (sectorsArr.size() >= 3) {
                        playerSectors_ = std::array<double, 3>{
                            sectorsArr.at(0).toDouble(),
                            sectorsArr.at(1).toDouble(),
                            sectorsArr.at(2).toDouble()
                        };
                    }
                }
                if (playerObj.contains(QStringLiteral("brake_temps"))) {
                    const auto btArr = playerObj.value(QStringLiteral("brake_temps")).toArray();
                    if (btArr.size() >= 4) {
                        brakeTemps_ = WheelValues{
                            btArr.at(0).toDouble(),
                            btArr.at(1).toDouble(),
                            btArr.at(2).toDouble(),
                            btArr.at(3).toDouble()
                        };
                    }
                }
            }
        }
    }

    if (!packetProcessed && !hasData()) {
        opponents_.clear();
        playerPosition_.reset();
        gapAhead_.reset();
        gapBehind_.reset();
        opponentAhead_.reset();
        opponentBehind_.reset();
        playerSectors_.reset();
        brakeTemps_.reset();
    }
}

} // namespace raceengineer
