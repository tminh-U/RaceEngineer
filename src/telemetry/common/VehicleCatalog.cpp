#include "telemetry/common/VehicleCatalog.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace raceengineer {
namespace {
using Catalog = QHash<QString, VehicleClass>;

Catalog readCatalog(const char* path, const char* simulator)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (document.object().value(QStringLiteral("schema_version")).toInt() != 1
        || document.object().value(QStringLiteral("simulator")).toString() != QString::fromLatin1(simulator)) return {};
    Catalog catalog;
    for (const auto value : document.object().value(QStringLiteral("cars")).toArray()) {
        const auto row = value.toObject();
        const QString model = row.value(QStringLiteral("car_model")).toString().trimmed().toCaseFolded();
        const QString category = row.value(QStringLiteral("category")).toString();
        if (model.isEmpty() || category.isEmpty()) continue;
        catalog.insert(model, {category.toStdString(), row.value(QStringLiteral("subclass")).toString().toStdString()});
    }
    return catalog;
}
}

VehicleClass classifyVehicle(const Simulator simulator, const std::string_view carModel)
{
    static const Catalog ac = readCatalog(":/vehicle_catalog/ac_cars.json", "sim_ac");
    static const Catalog acc = readCatalog(":/vehicle_catalog/acc_cars.json", "sim_acc");
    const Catalog* catalog = simulator == Simulator::AssettoCorsa ? &ac
        : simulator == Simulator::AssettoCorsaCompetizione ? &acc : nullptr;
    if (!catalog || carModel.empty()) return {};
    const QString key = QString::fromUtf8(carModel.data(), static_cast<qsizetype>(carModel.size()))
        .trimmed().toCaseFolded();
    return catalog->value(key);
}

} // namespace raceengineer
