#pragma once
// QML view model for the EDIT timeline screen (10th tab). Tracks run top to
// bottom in six-track pages, time runs left to right in session beats; the
// waveform thumbnails come from the `.peaks` sidecars (pi/audio/peaks.hpp) and
// a missing/stale sidecar is a placeholder block, never a scan on the paint
// path. Region edits are the frozen session v2 takes.edit entries; every op
// writes that list and re-applies it through AudioGlue, so the session file,
// the dirty fingerprint and the engine cannot disagree.
//
// The destructive RENDER op is deliberately explicit and refused: the core
// take player documents that render-to-new-take is not part of its API (see
// timeline::kRenderMissingApi), so the UI names the missing call instead of
// pretending a render happened.
#include <QObject>
#include <QString>
#include <QVariantList>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "audio/peaks.hpp"
#include "audio/take_player.hpp"
#include "audio_glue.hpp"
#include "link_client.hpp"
#include "recorder_view_model.hpp"
#include "session_settings.hpp"
#include "timeline_model.hpp"

namespace mnky::app {

class TimelineViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList lanes READ lanes NOTIFY lanesChanged)
  Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
  Q_PROPERTY(int pageCount READ pageCount CONSTANT)
  Q_PROPERTY(int tracksPerPage READ tracksPerPage CONSTANT)
  Q_PROPERTY(int selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY selectedTrackChanged)
  Q_PROPERTY(QString selectedTrackName READ selectedTrackName NOTIFY selectedTrackChanged)
  Q_PROPERTY(double viewStartBeats READ viewStartBeats WRITE setViewStartBeats NOTIFY viewChanged)
  Q_PROPERTY(double viewBeats READ viewBeats NOTIFY viewChanged)
  Q_PROPERTY(double totalBeats READ totalBeats NOTIFY viewChanged)
  Q_PROPERTY(double playheadBeats READ playheadBeats NOTIFY playheadChanged)
  Q_PROPERTY(QString playheadText READ playheadText NOTIFY playheadChanged)
  Q_PROPERTY(double cursorBeats READ cursorBeats WRITE setCursorBeats NOTIFY cursorChanged)
  Q_PROPERTY(bool selectionActive READ selectionActive NOTIFY selectionChanged)
  Q_PROPERTY(double inBeats READ inBeats NOTIFY selectionChanged)
  Q_PROPERTY(double outBeats READ outBeats NOTIFY selectionChanged)
  Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList punchMarkers READ punchMarkers NOTIFY lanesChanged)
  Q_PROPERTY(bool loopEnabled READ loopEnabled NOTIFY loopChanged)
  Q_PROPERTY(double loopStartBeats READ loopStartBeats NOTIFY loopChanged)
  Q_PROPERTY(double loopEndBeats READ loopEndBeats NOTIFY loopChanged)
  Q_PROPERTY(QString loopText READ loopText NOTIFY loopChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString opsText READ opsText NOTIFY statusChanged)
  Q_PROPERTY(QString lastOpText READ lastOpText NOTIFY statusChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
  Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
  Q_PROPERTY(QVariantList previewWave READ previewWave NOTIFY previewChanged)
  Q_PROPERTY(QString previewText READ previewText NOTIFY previewChanged)
  Q_PROPERTY(double previewGainDb READ previewGainDb NOTIFY previewChanged)
  Q_PROPERTY(double previewFadeInBeats READ previewFadeInBeats NOTIFY previewChanged)
  Q_PROPERTY(double previewFadeOutBeats READ previewFadeOutBeats NOTIFY previewChanged)
  Q_PROPERTY(double previewLengthBeats READ previewLengthBeats NOTIFY previewChanged)

 public:
  TimelineViewModel(AppState& state, AudioGlue& audio, RecorderViewModel& recorder,
                    LinkClient& link, SessionSettings& settings,
                    QObject* parent = nullptr);

  QVariantList lanes() const { return lanes_; }
  int page() const { return page_; }
  void setPage(int page);
  int pageCount() const { return 3; }
  int tracksPerPage() const { return timeline::kTracksPerPage; }
  int selectedTrack() const { return selectedTrack_; }
  void setSelectedTrack(int track);
  QString selectedTrackName() const;
  double viewStartBeats() const { return viewStartBeats_; }
  void setViewStartBeats(double beats);
  double viewBeats() const { return viewBeats_; }
  double totalBeats() const { return totalBeats_; }
  double playheadBeats() const { return playheadBeats_; }
  QString playheadText() const;
  double cursorBeats() const { return cursorBeats_; }
  void setCursorBeats(double beats);
  bool selectionActive() const { return selection_.active; }
  double inBeats() const { return selection_.start(); }
  double outBeats() const { return selection_.end(); }
  QString selectionText() const;
  QVariantList punchMarkers() const { return punchMarkers_; }
  bool loopEnabled() const;
  double loopStartBeats() const;
  double loopEndBeats() const;
  QString loopText() const;
  QString statusText() const { return statusText_; }
  QString opsText() const { return opsText_; }
  QString lastOpText() const { return lastOpText_; }
  bool canUndo() const;
  QString undoText() const;
  QVariantList previewWave() const { return previewWave_; }
  QString previewText() const { return previewText_; }
  double previewGainDb() const { return previewGainDb_; }
  double previewFadeInBeats() const { return previewFadeInBeats_; }
  double previewFadeOutBeats() const { return previewFadeOutBeats_; }
  double previewLengthBeats() const { return previewLengthBeats_; }

  Q_INVOKABLE void nextPage();
  Q_INVOKABLE void prevPage();
  Q_INVOKABLE void scrollView(int direction);
  Q_INVOKABLE void zoom(int direction);
  Q_INVOKABLE QString beatTextOf(double beats) const;
  Q_INVOKABLE void moveCursor(double beatDelta, int trackDelta);
  Q_INVOKABLE void beginSelection(int track, double beat);
  Q_INVOKABLE void updateSelection(double beat);
  Q_INVOKABLE void endSelection();
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void setInAtCursor();
  Q_INVOKABLE void setOutAtCursor();
  Q_INVOKABLE QString nudgeIn(double deltaBeats);
  Q_INVOKABLE QString nudgeOut(double deltaBeats);
  Q_INVOKABLE int regionCount() const;
  // Empty on success; a UI-facing reason on refusal (RENDER always refuses with
  // the missing core API name). Applies the selected op to the selected track.
  Q_INVOKABLE QString applyOp(int op, double gainDb, double fadeIn, double fadeOut);
  Q_INVOKABLE void setPreviewDraft(double gainDb, double fadeIn, double fadeOut);
  Q_INVOKABLE bool undo();
  // Screenshot runner helper: synchronously scans active takes whose sidecar is
  // missing, so the --screenshots run can show real waveforms. Not called by
  // the UI; the UI never waits for a scan.
  Q_INVOKABLE QString preparePeaksForDemo();

  // Controller timer: re-reads take pointers/sidecars, updates the playhead and
  // re-applies region edits after a tempo change.
  void refresh();
  // Project load/new replaced settings.takeEdit: drop caches/undo and re-apply.
  void reloadFromSettings();

 signals:
  void lanesChanged();
  void pageChanged();
  void selectedTrackChanged();
  void viewChanged();
  void playheadChanged();
  void cursorChanged();
  void selectionChanged();
  void loopChanged();
  void statusChanged();
  void undoChanged();
  void previewChanged();

 private:
  struct TrackSource {
    std::string key;
    std::filesystem::path path;
    double seconds = 0.0;
    std::uint32_t sampleRate = 0;
    std::uint64_t frames = 0;
  };
  struct PeakCache {
    std::string path;
    audio::PeakSummary summary;
    bool valid = false;
  };
  struct UndoEntry {
    std::vector<session::TakeRegion> regions;
  };

  std::vector<session::TakeRegion>& editList(int track);
  const std::vector<session::TakeRegion>& editList(int track) const;
  double takeLengthFor(int track, const std::string& key) const;
  timeline::TakeLengthFn lengthFnFor(int track) const;
  std::filesystem::path resolveTake(int track, const std::string& key) const;
  void refreshSources();
  void pumpScanner();
  void queueScan(int track);
  void applyTrackEdit(int track);
  void applyAllEdits();
  void rebuildLanes();
  void rebuildPunch();
  void rebuildPreview();
  void refreshViewWindow();
  void setStatus(const QString& text);
  void pushUndo(int track);
  void emitUndo();

  AppState& state_;
  AudioGlue& audio_;
  RecorderViewModel& recorder_;
  LinkClient& link_;
  SessionSettings& settings_;

  std::array<TrackSource, audio::kMaxInputs> sources_{};
  std::array<PeakCache, audio::kMaxInputs> peaks_{};
  std::array<std::vector<UndoEntry>, audio::kMaxInputs> undo_{};
  std::array<double, audio::kMaxInputs> trackBeats_{};
  mutable std::map<std::string, double> takeBeatsByKey_;
  std::map<std::string, audio::PeakSummary> sidecarCache_;

  std::unique_ptr<audio::PeakScanner> scanner_;
  std::string scanningPath_;
  int scanningTrack_ = -1;
  std::vector<int> scanQueue_;

  QVariantList lanes_;
  QVariantList punchMarkers_;
  QVariantList previewWave_;
  timeline::BeatRange selection_;
  int page_ = 0;
  int selectedTrack_ = 0;
  double viewStartBeats_ = 0.0;
  double viewBeats_ = 32.0;
  double totalBeats_ = 32.0;
  double playheadBeats_ = 0.0;
  double cursorBeats_ = 0.0;
  QString playheadText_;
  QString selectionText_;
  QString statusText_ = QStringLiteral("EDIT - select a lane, drag a region");
  QString opsText_;
  QString lastOpText_ = QStringLiteral("no region op yet");
  QString previewText_;
  bool previewDraftActive_ = false;
  double previewGainDb_ = 0.0;
  double previewFadeInBeats_ = 0.0;
  double previewFadeOutBeats_ = 0.0;
  double previewLengthBeats_ = 0.0;
  int appliedTempoX100_ = -1;
  std::string appliedEditSignature_;
};

}  // namespace mnky::app
