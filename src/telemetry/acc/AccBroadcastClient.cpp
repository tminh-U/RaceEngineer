#include "telemetry/acc/AccBroadcastClient.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringConverter>
#include <QUdpSocket>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>

namespace raceengineer {
namespace {

class PacketReader final {
public:
    explicit PacketReader(const QByteArray& data) : data_(data) {}

    quint8 u8()
    {
        if (!need(1)) return 0;
        return static_cast<quint8>(data_.at(offset_++));
    }

    quint16 u16()
    {
        if (!need(2)) return 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(data_.constData() + offset_);
        offset_ += 2;
        return static_cast<quint16>(bytes[0] | (static_cast<quint16>(bytes[1]) << 8));
    }

    qint32 i32()
    {
        if (!need(4)) return 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(data_.constData() + offset_);
        offset_ += 4;
        const quint32 bits = static_cast<quint32>(bytes[0])
            | (static_cast<quint32>(bytes[1]) << 8)
            | (static_cast<quint32>(bytes[2]) << 16)
            | (static_cast<quint32>(bytes[3]) << 24);
        return static_cast<qint32>(bits);
    }

    float f32() { return std::bit_cast<float>(static_cast<quint32>(i32())); }

    QString string()
    {
        const quint16 length = u16();
        if (!need(length)) return {};
        const QString result = QString::fromUtf8(data_.constData() + offset_, length);
        offset_ += length;
        return result;
    }

    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    bool need(qsizetype size)
    {
        if (!ok_ || size < 0 || offset_ + size > data_.size()) {
            ok_ = false;
            return false;
        }
        return true;
    }

    const QByteArray& data_;
    qsizetype offset_{0};
    bool ok_{true};
};

void appendU16(QByteArray& output, quint16 value)
{
    output.append(static_cast<char>(value & 0xff));
    output.append(static_cast<char>((value >> 8) & 0xff));
}

void appendI32(QByteArray& output, qint32 value)
{
    const quint32 bits = static_cast<quint32>(value);
    output.append(static_cast<char>(bits & 0xff));
    output.append(static_cast<char>((bits >> 8) & 0xff));
    output.append(static_cast<char>((bits >> 16) & 0xff));
    output.append(static_cast<char>((bits >> 24) & 0xff));
}

void appendString(QByteArray& output, const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    appendU16(output, static_cast<quint16>(std::min<qsizetype>(bytes.size(), 65535)));
    output.append(bytes.left(65535));
}

std::optional<double> lapSeconds(qint32 milliseconds)
{
    if (milliseconds < 0 || milliseconds == std::numeric_limits<qint32>::max()) {
        return std::nullopt;
    }
    return static_cast<double>(milliseconds) / 1000.0;
}

struct LapData final {
    std::optional<double> time;
    std::optional<std::array<double, 3>> sectors;
    bool invalid{false};
};

LapData readLap(PacketReader& reader)
{
    LapData lap;
    lap.time = lapSeconds(reader.i32());
    reader.u16(); // car index
    reader.u16(); // driver index
    const quint8 splitCount = reader.u8();
    std::array<double, 3> sectors{};
    bool hasAllSectors = splitCount >= 3;
    for (quint8 index = 0; index < splitCount; ++index) {
        const qint32 value = reader.i32();
        if (index < 3) {
            if (value == std::numeric_limits<qint32>::max() || value < 0) hasAllSectors = false;
            else sectors[index] = static_cast<double>(value) / 1000.0;
        }
    }
    lap.invalid = reader.u8() != 0;
    reader.u8(); // valid for best
    reader.u8(); // out lap
    reader.u8(); // in lap
    if (hasAllSectors) lap.sectors = sectors;
    return lap;
}

QString decodeConfig(const QByteArray& bytes)
{
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff
        && static_cast<unsigned char>(bytes[1]) == 0xfe) {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        return decoder(QByteArrayView(bytes.constData() + 2, bytes.size() - 2));
    }
    if (bytes.size() >= 2 && bytes[1] == '\0') {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        return decoder(bytes);
    }
    return QString::fromUtf8(bytes);
}

} // namespace

struct AccBroadcastClient::Impl final {
    struct Driver final {
        QString name;
    };
    struct Car final {
        OpponentState state;
        std::vector<Driver> drivers;
        int activeDriver{0};
        int lastRecordedLap{-1};
        double lastRecordedTime{-1.0};
    };

    std::unique_ptr<QUdpSocket> socket;
    quint16 port{0};
    QString connectionPassword;
    QString commandPassword;
    qint32 connectionId{0};
    bool registered{false};
    std::unordered_map<int, Car> cars;
    std::chrono::steady_clock::time_point lastRegister{};

    void send(const QByteArray& packet)
    {
        if (socket && port > 0) socket->writeDatagram(packet, QHostAddress::LocalHost, port);
    }

    void registerClient()
    {
        QByteArray packet;
        packet.append(char(1));
        packet.append(char(4)); // current ACC broadcasting protocol
        appendString(packet, QStringLiteral("RaceEngineer"));
        appendString(packet, connectionPassword);
        appendI32(packet, 100);
        appendString(packet, commandPassword);
        send(packet);
        lastRegister = std::chrono::steady_clock::now();
    }

    void request(quint8 command)
    {
        QByteArray packet;
        packet.append(static_cast<char>(command));
        appendI32(packet, connectionId);
        send(packet);
    }

    void registration(PacketReader& reader)
    {
        connectionId = reader.i32();
        registered = reader.u8() != 0;
        reader.u8(); // read-only flag
        reader.string(); // error message
        if (reader.ok() && registered) {
            request(10); // entry list
            request(11); // track data
        }
    }

    void entryList(PacketReader& reader)
    {
        reader.i32(); // connection id
        const quint16 count = reader.u16();
        std::unordered_map<int, Car> refreshed;
        for (quint16 index = 0; index < count; ++index) {
            const int carIndex = reader.u16();
            const auto existing = cars.find(carIndex);
            if (existing != cars.end()) refreshed.emplace(carIndex, std::move(existing->second));
            else {
                Car car;
                car.state.carId = carIndex;
                refreshed.emplace(carIndex, std::move(car));
            }
        }
        if (reader.ok()) cars = std::move(refreshed);
    }

    void entryListCar(PacketReader& reader)
    {
        const int carIndex = reader.u16();
        auto& car = cars[carIndex];
        car.state.carId = carIndex;
        reader.u8(); // car model type
        car.state.teamName = reader.string().toStdString();
        car.state.raceNumber = reader.i32();
        reader.u8(); // cup category
        car.activeDriver = reader.u8();
        reader.u16(); // car nationality
        const quint8 driverCount = reader.u8();
        std::vector<Driver> drivers;
        drivers.reserve(driverCount);
        for (quint8 index = 0; index < driverCount; ++index) {
            const QString first = reader.string();
            const QString last = reader.string();
            const QString shortName = reader.string();
            reader.u8(); // category
            reader.u16(); // nationality
            QString name = (first + QStringLiteral(" ") + last).trimmed();
            if (name.isEmpty()) name = shortName;
            drivers.push_back({name});
        }
        if (!reader.ok()) return;
        car.drivers = std::move(drivers);
        if (car.activeDriver >= 0 && car.activeDriver < static_cast<int>(car.drivers.size())) {
            car.state.driverName = car.drivers[car.activeDriver].name.toStdString();
        }
    }

    void realtimeUpdate(PacketReader& reader)
    {
        reader.u16(); // event index
        reader.u16(); // session index
        reader.u8(); // session type
        reader.u8(); // phase
        reader.f32();
        reader.f32();
        reader.i32(); // focused car; playerCarID from shared memory is more reliable
    }

    void realtimeCar(PacketReader& reader)
    {
        const int carIndex = reader.u16();
        const int driverIndex = reader.u16();
        reader.u8(); // driver count (protocol v4)
        reader.u8(); // gear
        reader.f32();
        reader.f32();
        reader.f32();
        const quint8 location = reader.u8();
        const quint16 speed = reader.u16();
        const quint16 position = reader.u16();
        const quint16 classPosition = reader.u16();
        const quint16 trackPosition = reader.u16();
        const float splinePosition = reader.f32();
        const quint16 laps = reader.u16();
        reader.i32(); // delta to session best
        const LapData best = readLap(reader);
        const LapData last = readLap(reader);
        const LapData current = readLap(reader);
        if (!reader.ok()) return;

        auto& car = cars[carIndex];
        car.state.carId = carIndex;
        car.activeDriver = driverIndex;
        if (driverIndex >= 0 && driverIndex < static_cast<int>(car.drivers.size())) {
            car.state.driverName = car.drivers[driverIndex].name.toStdString();
        }
        car.state.speedKmh = speed;
        if (position > 0) car.state.position = position;
        if (classPosition > 0) car.state.classPosition = classPosition;
        car.state.trackPosition = trackPosition;
        car.state.splinePosition = splinePosition;
        car.state.completedLaps = laps;
        car.state.inPitLane = location >= 2 && location <= 4;
        car.state.bestLapTimeSeconds = best.time;
        car.state.previousLapTimeSeconds = last.time;
        car.state.currentLapTimeSeconds = current.time;
        car.state.sectorTimesSeconds = current.sectors;
        if (last.time && !last.invalid
            && (car.lastRecordedLap != laps || std::abs(car.lastRecordedTime - *last.time) > 0.001)) {
            car.state.recentLapTimesSeconds.push_back(*last.time);
            if (car.state.recentLapTimesSeconds.size() > 5) {
                car.state.recentLapTimesSeconds.erase(car.state.recentLapTimesSeconds.begin());
            }
            car.lastRecordedLap = laps;
            car.lastRecordedTime = *last.time;
        }
    }

    void process(const QByteArray& datagram)
    {
        PacketReader reader(datagram);
        switch (reader.u8()) {
        case 1: registration(reader); break;
        case 2: realtimeUpdate(reader); break;
        case 3: realtimeCar(reader); break;
        case 4: entryList(reader); break;
        case 6: entryListCar(reader); break;
        default: break;
        }
    }
};

AccBroadcastClient::AccBroadcastClient() : impl_(std::make_unique<Impl>()) {}
AccBroadcastClient::~AccBroadcastClient() = default;

bool AccBroadcastClient::start()
{
    stop();
    const QString configPath = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Assetto Corsa Competizione/Config/broadcasting.json"));
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto document = QJsonDocument::fromJson(decodeConfig(file.readAll()).toUtf8());
    if (!document.isObject()) return false;
    const QJsonObject config = document.object();
    const int configuredPort = config.value(QStringLiteral("updListenerPort")).toInt(
        config.value(QStringLiteral("udpListenerPort")).toInt());
    if (configuredPort <= 0 || configuredPort > 65535) return false;

    impl_->port = static_cast<quint16>(configuredPort);
    impl_->connectionPassword = config.value(QStringLiteral("connectionPassword")).toString();
    impl_->commandPassword = config.value(QStringLiteral("commandPassword")).toString();
    impl_->socket = std::make_unique<QUdpSocket>();
    if (!impl_->socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        stop();
        return false;
    }
    impl_->registerClient();
    return true;
}

void AccBroadcastClient::stop() noexcept
{
    if (impl_->socket && impl_->registered) {
        QByteArray packet;
        packet.append(char(9));
        appendI32(packet, impl_->connectionId);
        impl_->send(packet);
    }
    impl_->socket.reset();
    impl_->registered = false;
    impl_->connectionId = 0;
    impl_->port = 0;
    impl_->cars.clear();
}

void AccBroadcastClient::update()
{
    if (!impl_->socket) return;
    while (impl_->socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<qsizetype>(impl_->socket->pendingDatagramSize()));
        if (impl_->socket->readDatagram(datagram.data(), datagram.size()) >= 0) impl_->process(datagram);
    }
    if (!impl_->registered
        && std::chrono::steady_clock::now() - impl_->lastRegister > std::chrono::seconds(2)) {
        impl_->registerClient();
    }
}

std::vector<OpponentState> AccBroadcastClient::opponents(const int playerCarId) const
{
    std::vector<OpponentState> result;
    result.reserve(impl_->cars.size());
    for (const auto& [carId, car] : impl_->cars) {
        if (carId != playerCarId && (!car.state.driverName.empty() || car.state.position)) {
            result.push_back(car.state);
        }
    }
    return result;
}

std::optional<std::array<double, 3>> AccBroadcastClient::playerSectorTimes(const int playerCarId) const
{
    const auto it = impl_->cars.find(playerCarId);
    if (it != impl_->cars.end()) {
        return it->second.state.sectorTimesSeconds;
    }
    return std::nullopt;
}

} // namespace raceengineer
