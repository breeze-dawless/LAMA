# LFO Draw (custom shape editor)

Opens the editor for an LFO whose shape is set to **Custom**. The horizontal menu shows the drawn cycle.

The shape is made of points joined by segments that can be bent (Serum style). One point is always selected; the display shows it as a filled square and the pads light it white.

- **Pads**: set a point in that column at that height (a point already in the column just gets the new height). Shift + pad removes the points in the column. The pads show the visible part of the cycle.
- **X encoder** (black, top left): moves the selected point in time (shift: finer). **Pressed and turned**: zooms in and out (x1, x2, x4, x8) - the page you're on shows top right on the display (2/4) and on the sidebar.
- **Y encoder** (black, bottom left): selects the previous / next point. **Pressed and turned right**: splits the visible part into ever more equal parts (2, 4, 8, 16) by adding points along the shape; **pressed and turned left** takes the finest split back out. **Shift + press**: loads the next preset shape (triangle, sine, saw up, saw down, square).
- **Upper gold knob**: the selected point's height (shift: finer). **Press**: deletes the selected point.
- **Lower gold knob**: bends the segment after the selected point - turning right always bows it upwards. **Pressed and turned**: bends the segment before it.
- **Synth / Kit**: previous point, **MIDI / CV**: next point. **With shift**: previous / next page while zoomed in. The pads of the first sidebar column jump to a page too.
- **Select encoder**: selects points, like the Y encoder.
- **Undo / redo**: the second sidebar column - the **bottom pad undoes** (red), the **pad above it redoes** (green); each lights up bright while there's a step to take. **Shift + select encoder**: left undoes, right redoes, a step per detent - quick for many steps. Turning a knob on and on counts as one step; up to 24 steps.
- **Leaving**: **back** or a **press on the select encoder**, as in every menu (holding back leaves the whole menu). The undo history stays: opening the editor again on the same LFO carries on with it, as long as nothing else changed the shape in the meantime.

The first point sits at the start of the cycle and stays there. Up to 64 points per cycle. The shape is saved with the sound.

Available when the LFO shape is set to Custom.
