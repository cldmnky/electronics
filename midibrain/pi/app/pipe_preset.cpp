#include "pipe_preset.hpp"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <string>

#include "../midi/midifx.hpp"
#include "pipe_ui.hpp"

namespace mnky::app::pipe_preset {
namespace {

namespace generated = mnky::midifx::generated;

QString valueKey(const char* key) { return QString::fromLatin1(key); }

}  // namespace

QString validateName(const QString& name) {
  if (name.isEmpty()) return QStringLiteral("name is empty");
  if (name.size() > 64) return QStringLiteral("name is longer than 64 characters");
  if (name.startsWith(QLatin1Char('.'))) return QStringLiteral("no leading dot");
  for (const QChar character : name) {
    const unsigned short code = character.unicode();
    if (code < 0x20 || code > 0x7e) return QStringLiteral("use printable ASCII only");
    if (character == QLatin1Char('/') || character == QLatin1Char('\\')) {
      return QStringLiteral("no path separators");
    }
  }
  if (name.endsWith(QLatin1Char(' '))) return QStringLiteral("no trailing space");
  return {};
}

QString resolvePath(const QString& entry, const QString& projectDirectory,
                    std::filesystem::path& out) {
  const QString trimmed = entry.trimmed();
  if (trimmed.isEmpty()) return QStringLiteral("enter a chain file name");
  const QString name =
      trimmed.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)
          ? trimmed
          : trimmed + QStringLiteral(".json");
  if (!name.contains(QLatin1Char('/'))) {
    const QString invalid = validateName(name);
    if (!invalid.isEmpty()) return invalid;
    if (projectDirectory.trimmed().isEmpty()) {
      return QStringLiteral("no project open; type a path with / or load a project");
    }
    out = std::filesystem::path(projectDirectory.toStdString()) / name.toStdString();
    return {};
  }
  std::filesystem::path candidate(name.toStdString());
  if (candidate.is_relative()) {
    if (projectDirectory.trimmed().isEmpty()) {
      return QStringLiteral("no project open; type a full path");
    }
    candidate = std::filesystem::path(projectDirectory.toStdString()) / candidate;
  }
  out = candidate;
  return {};
}

QByteArray encode(const ChainFile& chain) {
  QJsonObject root;
  root.insert(valueKey("format"), QString::fromLatin1(kFormat));
  root.insert(valueKey("schema"), static_cast<int>(generated::kSchemaVersion));
  if (!chain.name.isEmpty()) root.insert(valueKey("name"), chain.name);
  QJsonArray pipes;
  for (const auto& pipe : chain.pipes) {
    QJsonObject object;
    object.insert(valueKey("t"), static_cast<int>(pipe.type));
    object.insert(valueKey("bypass"), pipe.bypass);
    object.insert(valueKey("chm"), static_cast<int>(pipe.chMask));
    QJsonArray params;
    for (const int value : pipe.params) params.append(value);
    object.insert(valueKey("params"), params);
    pipes.append(object);
  }
  root.insert(valueKey("pipes"), pipes);
  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QString decode(const QByteArray& bytes, ChainFile& out) {
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
  if (document.isNull() || !document.isObject()) {
    return QStringLiteral("not a JSON object: ") + parseError.errorString();
  }
  const QJsonObject root = document.object();
  if (root.value(valueKey("format")).toString() != QString::fromLatin1(kFormat)) {
    return QStringLiteral("not a pipe chain file (format is not ") +
           QString::fromLatin1(kFormat) + QStringLiteral(")");
  }
  const int schema = root.value(valueKey("schema")).toInt(0);
  if (schema < 1) return QStringLiteral("the chain file has no schema version");
  if (schema > static_cast<int>(generated::kSchemaVersion)) {
    return QStringLiteral("chain schema v%1 is newer than this build (v%2)")
        .arg(schema)
        .arg(static_cast<int>(generated::kSchemaVersion));
  }
  const QJsonValue pipesValue = root.value(valueKey("pipes"));
  if (!pipesValue.isArray()) return QStringLiteral("the chain file has no pipes array");
  const QJsonArray array = pipesValue.toArray();
  if (array.size() > static_cast<int>(midi::kMaxRoutePipes)) {
    return QStringLiteral("the chain has %1 pipes; the limit is %2")
        .arg(array.size())
        .arg(static_cast<int>(midi::kMaxRoutePipes));
  }

  out.pipes.clear();
  out.pipes.reserve(static_cast<std::size_t>(array.size()));
  out.name = root.value(valueKey("name")).toString();
  for (int pipeIndex = 0; pipeIndex < array.size(); ++pipeIndex) {
    const QString where = QStringLiteral("pipe %1").arg(pipeIndex + 1);
    const QJsonValue value = array.at(pipeIndex);
    if (!value.isObject()) return where + QStringLiteral(" is not an object");
    const QJsonObject object = value.toObject();
    const QJsonValue typeValue = object.value(valueKey("t"));
    if (!typeValue.isDouble()) return where + QStringLiteral(" has no type id");
    const int typeId = typeValue.toInt();
    if (!pipeui::isKnown(typeId)) {
      return QStringLiteral("%1 has unknown type id %2; the schema defines 1..%3")
          .arg(where)
          .arg(typeId)
          .arg(static_cast<int>(generated::kPipeCount));
    }
    session::MidiPipe pipe;
    pipe.type = static_cast<session::MidiPipeType>(typeId);
    pipe.bypass = object.value(valueKey("bypass")).toBool(false);
    const int chMask = object.value(valueKey("chm")).toInt(0xFFFF);
    pipe.chMask = static_cast<std::uint16_t>(std::clamp(chMask, 0, 0xFFFF));

    const int count = pipeui::paramCount(typeId);
    pipe.params.assign(static_cast<std::size_t>(count), 0);
    const mnky::midifx::generated::PipeSpec* spec =
        pipeui::pipeSpec(typeId);
    for (int index = 0; index < count; ++index) {
      pipe.params[static_cast<std::size_t>(index)] =
          spec->params[static_cast<std::size_t>(index)].defaultValue;
    }
    const QJsonValue paramsValue = object.value(valueKey("params"));
    if (paramsValue.isArray()) {
      const QJsonArray params = paramsValue.toArray();
      if (params.size() > static_cast<int>(midi::kMaxPipeParams)) {
        return QStringLiteral("%1 has %2 params; the limit is %3")
            .arg(where)
            .arg(params.size())
            .arg(static_cast<int>(midi::kMaxPipeParams));
      }
      if (params.size() > count) {
        return QStringLiteral("%1 has %2 params; type %3 defines %4")
            .arg(where)
            .arg(params.size())
            .arg(typeId)
            .arg(count);
      }
      for (int index = 0; index < params.size(); ++index) {
        const QJsonValue paramValue = params.at(index);
        if (!paramValue.isDouble()) {
          return QStringLiteral("%1 param %2 is not a number").arg(where).arg(index + 1);
        }
        pipe.params[static_cast<std::size_t>(index)] = pipeui::clamp(
            spec->params[static_cast<std::size_t>(index)], paramValue.toInt());
      }
    }
    out.pipes.push_back(std::move(pipe));
  }
  return {};
}

QString write(const std::filesystem::path& file, const ChainFile& chain) {
  QSaveFile handle(QString::fromStdString(file.string()));
  if (!handle.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return QStringLiteral("cannot write ") + QString::fromStdString(file.string());
  }
  const QByteArray bytes = encode(chain);
  if (handle.write(bytes) != bytes.size()) {
    handle.cancelWriting();
    return QStringLiteral("cannot write ") + QString::fromStdString(file.string());
  }
  if (!handle.commit()) {
    return QStringLiteral("cannot write ") + QString::fromStdString(file.string());
  }
  return {};
}

QString read(const std::filesystem::path& file, ChainFile& out) {
  QFile handle(QString::fromStdString(file.string()));
  if (!handle.exists()) {
    return QStringLiteral("no such file: ") + QString::fromStdString(file.string());
  }
  if (!handle.open(QIODevice::ReadOnly)) {
    return QStringLiteral("cannot read ") + QString::fromStdString(file.string());
  }
  return decode(handle.readAll(), out);
}

}  // namespace mnky::app::pipe_preset
