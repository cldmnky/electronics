#pragma once
// QML view model for the EDIT popup (docs/daw-plan.md W5): one take, one
// waveform, one region selection. Time runs left to right in session beats;
// the waveform comes from the take's `.peaks` sidecar (pi/audio/peaks.hpp) at
// the sidecar's own resolution, and a missing/stale sidecar shows a SCANNING
// badge while the background scanner fills it in - never a dead text block and
// never a scan on the paint path. Region edits are the frozen session v2
// takes.edit entries; every op writes that list and re-applies it through
// AudioGlue, so the session file, the dirty fingerprint and the engine cannot
// disagree.
//
// The popup is modal and single-take, so the multi-track lane/page model the
// retired EDIT tab used is gone: `selectedTrack` is the track the popup edits,
// and the view window is take-relative.
//
// The destructive RENDER action is not a region op here: the popup's RENDER
// REGION TO TRACK calls AppController::renderRegionToTrack(), which uses the
// core Engine::renderRegionToTake() and then the take -> track flow.
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
  // EDIT popup: open while the take is being edited, one track at a time.
  Q_PROPERTY(bool editOpen READ editOpen NOTIFY editOpenChanged)
  Q_PROPERTY(QString editTitle READ editTitle NOTIFY editChanged)
  Q_PROPERTY(QString editMetaText READ editMetaText NOTIFY editChanged)
  Q_PROPERTY(QString editTrackText READ editTrackText NOTIFY selectedTrackChanged)
  // 2 entries per column: [lo0, hi0, lo1, hi1, ...] peak magnitudes 0..1 over
  // the current view window. Empty while the take has no readable sidecar.
  Q_PROPERTY(QVariantList editWave READ editWave NOTIFY editWaveChanged)
  Q_PROPERTY(int editWaveColumns READ editWaveColumns NOTIFY editWaveChanged)
  // Missing-sidecar badge: the background scanner is filling this take's
  // sidecar; the popup shows `SCANNING nn%` instead of an empty block.
  Q_PROPERTY(bool editScanning READ editScanning NOTIFY scanChanged)
  Q_PROPERTY(QString editScanText READ editScanText NOTIFY scanChanged)
  // Selected region op and its live drafts (plan W5 ops strip).
  Q_PROPERTY(int editOp READ editOp WRITE selectEditOp NOTIFY editOpChanged)
  Q_PROPERTY(QString editOpName READ editOpName NOTIFY editOpChanged)
  Q_PROPERTY(double editGainDb READ previewGainDb NOTIFY previewChanged)
  Q_PROPERTY(double editFadeInBeats READ previewFadeInBeats NOTIFY previewChanged)
  Q_PROPERTY(double editFadeOutBeats READ previewFadeOutBeats NOTIFY previewChanged)
  Q_PROPERTY(QString editFooterText READ editFooterText NOTIFY selectionChanged)
  Q_PROPERTY(QString editHintText READ editHintText NOTIFY editChanged)
  // The popup playhead (Bluebox rule: a tap sets the last touch point); while
  // the transport rolls it follows the session position.
  Q_PROPERTY(double editPlayheadBeats READ editPlayheadBeats NOTIFY playheadChanged)
  Q_PROPERTY(double viewStartBeats READ viewStartBeats WRITE setViewStartBeats NOTIFY viewChanged)
  Q_PROPERTY(double viewBeats READ viewBeats NOTIFY viewChanged)
  Q_PROPERTY(double takeBeats READ takeBeats NOTIFY editChanged)
  Q_PROPERTY(int selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY selectedTrackChanged)
  Q_PROPERTY(QString selectedTrackName READ selectedTrackName NOTIFY selectedTrackChanged)
  Q_PROPERTY(double playheadBeats READ playheadBeats NOTIFY playheadChanged)
  Q_PROPERTY(QString playheadText READ playheadText NOTIFY playheadChanged)
  Q_PROPERTY(double cursorBeats READ cursorBeats WRITE setCursorBeats NOTIFY cursorChanged)
  Q_PROPERTY(bool selectionActive READ selectionActive NOTIFY selectionChanged)
  Q_PROPERTY(double inBeats READ inBeats NOTIFY selectionChanged)
  Q_PROPERTY(double outBeats READ outBeats NOTIFY selectionChanged)
  Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList punchMarkers READ punchMarkers NOTIFY punchChanged)
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
  // The region the draft applies to, and its stored values: the popup draws the
  // stored shape as BEFORE and the draft as AFTER (plan W5 live preview).
  Q_PROPERTY(bool previewRegionFound READ previewRegionFound NOTIFY previewChanged)
  Q_PROPERTY(double previewRegionStartBeats READ previewRegionStartBeats NOTIFY previewChanged)
  Q_PROPERTY(double previewRegionLengthBeats READ previewRegionLengthBeats NOTIFY previewChanged)
  Q_PROPERTY(double previewRegionGainDb READ previewRegionGainDb NOTIFY previewChanged)
  Q_PROPERTY(double previewRegionFadeInBeats READ previewRegionFadeInBeats NOTIFY previewChanged)
  Q_PROPERTY(double previewRegionFadeOutBeats READ previewRegionFadeOutBeats NOTIFY previewChanged)

 public:
  // Bounded per-track undo depth. Every op that changes the region list pushes
  // the previous list, so the last kUndoDepth edits of a take stay reversible
  // for the whole session (closing/reopening the popup or switching tracks does
  // not drop the stack; a project load does, by design).
  static constexpr std::size_t kUndoDepth = 32;
  TimelineViewModel(AppState& state, AudioGlue& audio, RecorderViewModel& recorder,
                    LinkClient& link, SessionSettings& settings,
                    QObject* parent = nullptr);

  bool editOpen() const { return editOpen_; }
  QString editTitle() const;
  QString editMetaText() const;
  // Sounding length of the committed region list in beats (the sum of the
  // regions, so a DELETE in the middle removes its span; 0 with no edit).
  // TRIM/DELETE move it, so the popup states the duration change.
  double editedBeats() const;
  QString editTrackText() const;
  QVariantList editWave() const { return editWave_; }
  int editWaveColumns() const { return editWaveColumns_; }
  bool editScanning() const { return editScanning_; }
  QString editScanText() const { return editScanText_; }
  int editOp() const { return editOp_; }
  // QML and the panel both select an op card; it is the Q_PROPERTY write
  // function and an invokable, so a tap and a ring stop use the same path.
  Q_INVOKABLE void selectEditOp(int op);
  QString editOpName() const;
  QString editFooterText() const;
  QString editHintText() const;
  double editPlayheadBeats() const { return editPlayheadBeats_; }
  double viewStartBeats() const { return viewStartBeats_; }
  void setViewStartBeats(double beats);
  double viewBeats() const { return viewBeats_; }
  double takeBeats() const { return trackBeats_[static_cast<std::size_t>(selectedTrack_)]; }
  int selectedTrack() const { return selectedTrack_; }
  void setSelectedTrack(int track);
  QString selectedTrackName() const;
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
  bool previewRegionFound() const { return previewRegionFound_; }
  double previewRegionStartBeats() const { return previewRegionStartBeats_; }
  double previewRegionLengthBeats() const { return previewRegionLengthBeats_; }
  double previewRegionGainDb() const { return previewRegionGainDb_; }
  double previewRegionFadeInBeats() const { return previewRegionFadeInBeats_; }
  double previewRegionFadeOutBeats() const { return previewRegionFadeOutBeats_; }

  // The popup's lifecycle: openEdit() makes `track` the edit subject, fits the
  // view to its take and clears the selection.
  Q_INVOKABLE void openEdit(int track);
  Q_INVOKABLE void closeEdit();
  Q_INVOKABLE void scrollView(int direction);
  Q_INVOKABLE void zoom(int direction);
  // Pinch/two-finger zoom: a factor > 1 zooms in, < 1 zooms out, around the
  // cursor (or the view centre when there is none).
  Q_INVOKABLE void zoomBy(double factor);
  Q_INVOKABLE QString beatTextOf(double beats) const;
  // Popup geometry helpers: a 0..1 ratio across the waveform and the beat it
  // addresses inside the current view window.
  Q_INVOKABLE double beatAtRatio(double ratio) const;
  Q_INVOKABLE double ratioOfBeat(double beats) const;
  Q_INVOKABLE void moveCursor(double beatDelta, int trackDelta);
  Q_INVOKABLE void beginSelection(int track, double beat);
  Q_INVOKABLE void updateSelection(double beat);
  Q_INVOKABLE void endSelection();
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void setInAtCursor();
  Q_INVOKABLE void setOutAtCursor();
  // Drag targets for the two edge handles; the popup calls these on every move
  // and shows the value popup itself.
  Q_INVOKABLE double setInBeats(double beats);
  Q_INVOKABLE double setOutBeats(double beats);
  // Bluebox rule: a tap sets the playhead and the edit cursor to the last touch
  // point, so the next gesture starts from there.
  Q_INVOKABLE void seekTo(double beats);
  Q_INVOKABLE QString nudgeIn(double deltaBeats);
  Q_INVOKABLE QString nudgeOut(double deltaBeats);
  Q_INVOKABLE int regionCount() const;
  // Empty on success; a UI-facing reason on refusal. Applies the selected op to
  // the popup's track.
  Q_INVOKABLE QString applyOp(int op, double gainDb, double fadeIn, double fadeOut);
  Q_INVOKABLE void setPreviewDraft(double gainDb, double fadeIn, double fadeOut);
  Q_INVOKABLE bool undo();
  // Empty on success; a UI-facing reason on refusal (no take, no region).
  Q_INVOKABLE QString applyEditOp();
  Q_INVOKABLE void setEditGainDb(double gainDb);
  Q_INVOKABLE void setEditFadeIn(double beats);
  Q_INVOKABLE void setEditFadeOut(double beats);
  // Screenshot runner helper: synchronously scans active takes whose sidecar is
  // missing, so the --screenshots run can show real waveforms. Not called by
  // the UI; the UI never waits for a scan.
  Q_INVOKABLE QString preparePeaksForDemo();
  // The popup's missing-peaks action: rescans the take and writes its .peaks
  // sidecar (the same background scanner the popup uses).
  Q_INVOKABLE QString renderPeaks();

  // Controller timer: re-reads take pointers/sidecars, updates the playhead and
  // re-applies region edits after a tempo change.
  void refresh();
  // Project load/new replaced settings.takeEdit: drop caches/undo and re-apply.
  void reloadFromSettings();

 signals:
  void editOpenChanged();
  void editChanged();
  void editWaveChanged();
  void scanChanged();
  void editOpChanged();
  void selectedTrackChanged();
  void viewChanged();
  void playheadChanged();
  void cursorChanged();
  void selectionChanged();
  void loopChanged();
  void punchChanged();
  void statusChanged();
  void undoChanged();
  void previewChanged();

 private:
  struct TrackSource {
    std::string key;
    std::filesystem::path path;
    double seconds = 0.0;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
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
  // The applied region list resolved into what the popup waveform draws: the
  // committed gain, fades and normalize scale per region span. EDIT shows the
  // take AFTER the ops that are committed, so APPLY changes the picture instead
  // of only the engine's copy of the region list.
  struct RegionShape {
    double start = 0.0;
    double end = 0.0;
    double gain = 1.0;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
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
  void rebuildPunch();
  void rebuildPreview();
  void rebuildEditWave();
  void refreshViewWindow();
  void updateScanBadge();
  void setStatus(const QString& text);
  void pushUndo(int track);
  void emitUndo();
  std::vector<RegionShape> appliedShapes(int track) const;
  static double shapeGainAt(const std::vector<RegionShape>& shapes, double beat);

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
  // Bumped whenever a peak summary is published, so the popup wave cache knows
  // the sidecar arrived.
  int peakGeneration_ = 0;

  QVariantList punchMarkers_;
  QVariantList previewWave_;
  QVariantList editWave_;
  timeline::BeatRange selection_;
  bool editOpen_ = false;
  int editWaveColumns_ = 0;
  int editOp_ = 0;
  int selectedTrack_ = 0;
  double viewStartBeats_ = 0.0;
  double viewBeats_ = 32.0;
  // Extent the view can zoom out to: the edited take's length (the popup is
  // take-relative; there is no multi-track session window any more).
  double totalBeats_ = 32.0;
  double playheadBeats_ = 0.0;
  double editPlayheadBeats_ = 0.0;
  double cursorBeats_ = 0.0;
  bool editScanning_ = false;
  QString playheadText_;
  QString selectionText_;
  QString statusText_ = QStringLiteral("EDIT - drag a region, then run an op");
  QString opsText_;
  QString lastOpText_ = QStringLiteral("no region op yet");
  QString previewText_;
  QString editWaveSignature_;
  QString editScanText_;
  bool previewDraftActive_ = false;
  double previewGainDb_ = 0.0;
  double previewFadeInBeats_ = 0.0;
  double previewFadeOutBeats_ = 0.0;
  double previewLengthBeats_ = 0.0;
  bool previewRegionFound_ = false;
  double previewRegionStartBeats_ = 0.0;
  double previewRegionLengthBeats_ = 0.0;
  double previewRegionGainDb_ = 0.0;
  double previewRegionFadeInBeats_ = 0.0;
  double previewRegionFadeOutBeats_ = 0.0;
  int appliedTempoX100_ = -1;
  std::string appliedEditSignature_;
};

}  // namespace mnky::app
