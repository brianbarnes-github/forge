# Songsmith TODO

Next up, in the order the user listed them. Each item gets its own design (brainstorm) before work starts.

- [ ] 1. **Ghost playhead line while dragging a volume slider.** Dragging a track head's volume slider leaves a stray playhead line on screen. Find what repaints or draws the playhead during the drag and fix it.
- [ ] 2. **Volume mixer as a dockable panel.** A mixer view (per-track volume, mute, solo) as a fourth dock panel (`panelDefs()` entry plus a View menu item). It would need a hand-written menu entry, since the View items are not yet generated from the registry.
- [ ] 3. **Time selection with looping.** Select a time range on the timeline and loop playback over it.
- [ ] 4. **Favourite commands in the Transport bar area.** Let the user pin chosen commands as buttons next to the transport controls.

## Older open items (from the dock-panels handoff)

- [ ] A floating editor left visible reopens empty at every launch and centres on the primary display, not over the main window.
- [ ] Floated panels do not forward Ctrl+S / Ctrl+Z / Delete to the main window.
- [ ] The Dock and x buttons inside a floating window destroy that window during their own click (no crash known; check on Windows).
- [ ] View menu items are not generated from the `panelDefs()` registry.
