pragma Singleton
import QtQuick

// Shared value formatters used across panels (Theme's sibling: Theme
// owns look, Format owns text).
QtObject {
    // "42s" / "3m 12s" / "1h 4m" — wall-clock durations for chunk and
    // job tables.
    function duration(ms) {
        const s = Math.floor(ms / 1000)
        if (s < 60) return s + "s"
        const m = Math.floor(s / 60)
        if (m < 60) return m + "m " + (s % 60) + "s"
        return Math.floor(m / 60) + "h " + (m % 60) + "m"
    }
}
