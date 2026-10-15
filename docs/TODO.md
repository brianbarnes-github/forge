# Songsmith TODO

Next up, in the order the user listed them. Each item gets its own design (brainstorm) before work starts.

- [x] 1. **Ghost playhead line while dragging a volume slider.** Dragging a track head's volume slider leaves a stray playhead line on screen. Find what repaints or draws the playhead during the drag and fix it. Fixed in the follow-up commit: `PlayheadDirtyTracker` now tracks every line drawn (partial row repaints at the live x no longer make it forget the full-height line the timer drew earlier).
- [x] 2. **Volume mixer as a dockable panel.** Done: the `mixer` dock panel (per-track pan, mute/solo and volume faders plus a pinned master fader), a hand-written View → Mixer item, and a per-track `playbackPan` override. See `docs/ARCHITECTURE.md` §9.19.
- [x] 3. **Time selection with looping.** Select a time range on the timeline and loop playback over it. Done: left-drag in a track's instrument band draws a session-only loop range (grid-snapped, Alt for free, drag inside to move), ruler brackets adjust or clear it, Esc clears it, and the engine wraps sample-accurately. See `docs/ARCHITECTURE.md` §9.14.
- [ ] 4. **Favourite commands in the Transport bar area.** Let the user pin chosen commands as buttons next to the transport controls.

## Older open items (from the dock-panels handoff)

- [x] A floating editor left visible no longer reopens empty at launch: `SongsmithMainComponent::restoreDockLayout` opens it hidden (bounds kept), so the next double-click centres it over the main window or uses its saved bounds.
- [x] Floated panels forward Esc, Ctrl+S, Delete and undo/redo to the main window (`DockHost::onFloatingKey`, `SongsmithMainComponent::onFloatingShortcut`).
- [ ] The Dock and x buttons inside a floating window destroy that window during their own click (no crash known; check on Windows).
- [ ] View menu items are not generated from the `panelDefs()` registry.
