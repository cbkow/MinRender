import QtQuick

// Desktop-list selection model shared by the jobs, chunks and peers lists
// (UFB's FileBrowser vocabulary). Keys are stable row identities (job id,
// chunk id, node id) so a selection survives the lists' periodic model
// refreshes; rows are only used for ranges and keyboard movement.
//
//   click             select only this row (it becomes the anchor)
//   Cmd / Ctrl-click  toggle this row (it becomes the anchor)
//   Shift-click       range from the anchor to this row (anchor stays)
//   right-click       keep a selection containing the row, else select it
//   Up / Down / Home / End / PageUp / PageDown
//                     move the cursor; with Shift, extend from the anchor
//   Cmd / Ctrl-A      select all
//   Escape            clear the selection
//   Return / Enter    activated(currentKey)
//
// The owning list supplies rowCount and keyAt(row), forwards clicks and key
// presses, and listens to moved(row) to scroll the cursor into view.
// (Qt reports Cmd as ControlModifier on macOS; Meta is accepted too.)
QtObject {
    id: sel

    property int rowCount: 0
    property var keyAt: function(row) { return "" }
    // Rows a PageUp / PageDown jumps.
    property int pageRows: 10

    // key → true. Reassigned (never mutated) so bindings see the change.
    property var selected: ({})
    property string anchorKey: ""
    // The cursor: the row keyboard moves start from, and the "primary"
    // (the jobs list drives the detail panel from it).
    property string currentKey: ""

    readonly property int count: {
        let n = 0
        for (const k in selected) n += 1
        return n
    }

    signal moved(int row)
    signal activated(string key)

    function isSelected(k) { return selected[String(k)] === true }
    function keys() { return Object.keys(selected) }

    function rowOf(k) {
        k = String(k)
        for (let i = 0; i < rowCount; ++i)
            if (String(keyAt(i)) === k) return i
        return -1
    }

    function selectOnly(k) {
        k = String(k)
        const o = {}
        if (k !== "") o[k] = true
        selected = o
        anchorKey = k
        currentKey = k
    }

    function toggle(k) {
        k = String(k)
        const o = Object.assign({}, selected)
        if (o[k]) delete o[k]; else o[k] = true
        selected = o
        anchorKey = k
        if (o[k]) currentKey = k
        else if (currentKey === k) currentKey = Object.keys(o)[0] || ""
    }

    function extendTo(k) {
        k = String(k)
        const a = anchorKey !== "" ? rowOf(anchorKey) : -1
        const b = rowOf(k)
        if (a < 0 || b < 0) { selectOnly(k); return }
        const o = {}
        for (let i = Math.min(a, b); i <= Math.max(a, b); ++i)
            o[String(keyAt(i))] = true
        selected = o
        currentKey = k
    }

    function selectAll() {
        const o = {}
        for (let i = 0; i < rowCount; ++i) o[String(keyAt(i))] = true
        selected = o
        if (currentKey === "" && rowCount > 0) currentKey = String(keyAt(0))
        if (anchorKey === "") anchorKey = currentKey
    }

    // Clears the selection; the cursor stays where it is.
    function clear() {
        selected = ({})
        anchorKey = ""
    }

    function handleClick(row, mouse) {
        const k = String(keyAt(row))
        const mods = mouse.modifiers
        if (mods & Qt.ShiftModifier) extendTo(k)
        else if (mods & (Qt.ControlModifier | Qt.MetaModifier)) toggle(k)
        else selectOnly(k)
    }

    function handleContextClick(row) {
        const k = String(keyAt(row))
        if (!isSelected(k)) selectOnly(k)
    }

    // Returns true when the event was a selection key (the caller accepts it).
    function handleKey(event) {
        if (event.matches(StandardKey.SelectAll)) { selectAll(); return true }
        if (event.key === Qt.Key_Escape) {
            if (count === 0) return false
            clear()
            return true
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (currentKey === "") return false
            activated(currentKey)
            return true
        }
        if (rowCount === 0) return false
        const cur = currentKey !== "" ? rowOf(currentKey) : -1
        let n
        switch (event.key) {
        case Qt.Key_Down:     n = cur < 0 ? 0 : Math.min(rowCount - 1, cur + 1); break
        case Qt.Key_Up:       n = cur < 0 ? 0 : Math.max(0, cur - 1); break
        case Qt.Key_Home:     n = 0; break
        case Qt.Key_End:      n = rowCount - 1; break
        case Qt.Key_PageDown: n = Math.min(rowCount - 1, Math.max(cur, 0) + pageRows); break
        case Qt.Key_PageUp:   n = Math.max(0, Math.max(cur, 0) - pageRows); break
        default: return false
        }
        const k = String(keyAt(n))
        if (event.modifiers & Qt.ShiftModifier) {
            if (anchorKey === "") anchorKey = cur >= 0 ? String(keyAt(cur)) : k
            extendTo(k)
        } else {
            selectOnly(k)
        }
        moved(n)
        return true
    }
}
