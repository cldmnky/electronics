#include "pipe_ui.hpp"

#include <QVariantMap>
#include <QStringList>

#include <algorithm>
#include <cstdio>

namespace mnky::app::pipeui {
namespace {

namespace generated = mnky::midifx::generated;
namespace ui = mnky::midifx::ui;

}  // namespace

const PipeSpec* pipeSpec(int typeId) {
  if (typeId < 1 || typeId > static_cast<int>(generated::kPipeCount)) return nullptr;
  return &generated::kPipes[static_cast<std::size_t>(typeId - 1)];
}

int paramCount(int typeId) {
  const PipeSpec* spec = pipeSpec(typeId);
  return spec == nullptr ? 0 : static_cast<int>(spec->paramCount);
}

bool isKnown(int typeId) { return pipeSpec(typeId) != nullptr; }

bool isClocked(int typeId) {
  const PipeSpec* spec = pipeSpec(typeId);
  return spec != nullptr && spec->clocked;
}

QString pipeName(int typeId) {
  const PipeSpec* spec = pipeSpec(typeId);
  return spec == nullptr ? QStringLiteral("?") : QString::fromLatin1(spec->name);
}

QString pipeShortName(int typeId) {
  const PipeSpec* spec = pipeSpec(typeId);
  return spec == nullptr ? QStringLiteral("?") : QString::fromLatin1(spec->shortName);
}

int clamp(const ParamSpec& param, int value) {
  return std::clamp(value, param.min, param.max);
}

QString noteName(int note) {
  static constexpr const char* kNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                           "F#", "G",  "G#", "A",  "A#", "B"};
  const int clamped = std::clamp(note, 0, 127);
  const int octave = clamped / 12 - 1;
  char buffer[16] = {};
  std::snprintf(buffer, sizeof buffer, "%s%d", kNames[clamped % 12], octave);
  return QString::fromLatin1(buffer);
}

QString valueText(const ParamUiSpec& spec, int value) {
  const ParamSpec& param = *spec.param;
  const int clamped = clamp(param, value);
  switch (spec.format) {
    case ui::ValueFormat::Bool:
      return clamped != 0 ? QStringLiteral("ON") : QStringLiteral("OFF");
    case ui::ValueFormat::Choice:
      if (param.choices != nullptr && param.choiceCount > 0) {
        return QString::fromLatin1(
            param.choices[std::clamp(clamped, 0, param.choiceCount - 1)]);
      }
      return QString::number(clamped);
    case ui::ValueFormat::Percent:
      return QString::number(clamped) + QStringLiteral("%");
    case ui::ValueFormat::Milliseconds:
      return QString::number(clamped) + QStringLiteral(" ms");
    case ui::ValueFormat::Semitones:
      return (clamped > 0 ? QStringLiteral("+") : QString()) +
             QString::number(clamped) + QStringLiteral(" st");
    case ui::ValueFormat::Note:
      return noteName(clamped);
    case ui::ValueFormat::Plain:
      break;
  }
  return QString::number(clamped);
}

QString valueText(int typeId, int index, int value) {
  const ParamUiSpec* spec =
      ui::uiParamSpec(static_cast<std::uint8_t>(typeId), static_cast<std::size_t>(index));
  return spec == nullptr ? QString() : valueText(*spec, value);
}

QString editorKind(const ParamUiSpec& spec) {
  switch (spec.editor) {
    case ui::EditorKind::Toggle:
      return QStringLiteral("toggle");
    case ui::EditorKind::Choice:
      return QStringLiteral("choice");
    case ui::EditorKind::Knob:
      return QStringLiteral("knob");
    case ui::EditorKind::Stepper:
      break;
  }
  return QStringLiteral("stepper");
}

QVariantList editorRowsFor(const ParamSpec* params, const ParamUiSpec* ui,
                           std::size_t count, const std::vector<int>& values,
                           int first, int last) {
  QVariantList rows;
  if (params == nullptr || ui == nullptr) return rows;
  const int begin = std::max(0, first);
  const int end = std::min(last, static_cast<int>(count));
  for (int index = begin; index < end; ++index) {
    const ParamSpec& param = params[static_cast<std::size_t>(index)];
    const int value = index < static_cast<int>(values.size())
                          ? clamp(param, values[static_cast<std::size_t>(index)])
                          : param.defaultValue;
    QStringList choices;
    for (int choice = 0; choice < param.choiceCount; ++choice) {
      choices << QString::fromLatin1(param.choices[choice]);
    }
    QVariantMap row;
    row.insert(QStringLiteral("name"), QString::fromLatin1(param.label));
    row.insert(QStringLiteral("param"), index);
    row.insert(QStringLiteral("minimum"), param.min);
    row.insert(QStringLiteral("maximum"), param.max);
    row.insert(QStringLiteral("value"), value);
    row.insert(QStringLiteral("editor"), editorKind(ui[static_cast<std::size_t>(index)]));
    row.insert(QStringLiteral("choices"), choices);
    row.insert(QStringLiteral("valueText"),
               valueText(ui[static_cast<std::size_t>(index)], value));
    rows.push_back(row);
  }
  return rows;
}

QVariantList editorRows(int typeId, const std::vector<int>& values, int first,
                        int last) {
  const PipeSpec* pipe = pipeSpec(typeId);
  const ui::PipeUiSpec* spec = ui::pipeUiSpec(static_cast<std::uint8_t>(typeId));
  if (pipe == nullptr || spec == nullptr) return {};
  return editorRowsFor(pipe->params, spec->params, spec->paramCount, values, first,
                       last);
}

int pageCount(std::size_t paramCount, int perPage) {
  if (perPage <= 0) return 1;
  return std::max(1, (static_cast<int>(paramCount) + perPage - 1) / perPage);
}

std::string learnTargetPath(int source, int destination, int pipe, int param) {
  char buffer[64] = {};
  std::snprintf(buffer, sizeof buffer, "routing.pipe.%d.%d.%d.%d", source,
                destination, pipe, param);
  return buffer;
}

}  // namespace mnky::app::pipeui
