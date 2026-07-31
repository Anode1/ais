/// Collapsing a recall page to one row per RECORD.
///
/// recall returns one hit per LINK, and a record can hold several. Rendering
/// those as separate rows was actively dangerous rather than merely untidy:
/// there is no "delete one link" operation, so deleting any of those rows
/// deleted the whole record -- every link -- and the delete path removed the
/// FIRST row carrying that id, which was usually not the row the user tapped.
/// Three acceptance testers hit it independently; one watched a row they had
/// not touched vanish while the one they deleted stayed on screen.
///
/// Kept here, out of the widget, so it can be tested without a Flutter binding.
library;

import 'ais_ffi.dart';

/// The result of collapsing a page: the rows to show, and for each record id the
/// number of FURTHER links it holds beyond the one displayed.
class GroupedRows {
  final List<Hit> rows;
  final Map<int, int> extra;
  const GroupedRows(this.rows, this.extra);
}

/// Keep the first hit per id, in the order the engine returned them, and count
/// the rest. Order matters: it is the engine's relevance/id order, and shuffling
/// it would make the same query look different between pages.
GroupedRows oneRowPerRecord(List<Hit> page) {
  final rows = <Hit>[];
  final extra = <int, int>{};
  final seen = <int>{};
  for (final h in page) {
    if (seen.add(h.id)) {
      rows.add(h);
    } else {
      extra[h.id] = (extra[h.id] ?? 0) + 1;
    }
  }
  return GroupedRows(rows, extra);
}
