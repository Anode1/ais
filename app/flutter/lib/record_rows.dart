/// Collapsing a recall page to one row per RECORD.
///
/// recall returns one hit per LINK, and a record can hold several. There is no
/// "delete one link" operation: a per-link row deletes the whole record, and the
/// delete path removes the FIRST row carrying that id, not the row tapped.
///
/// Kept out of the widget so it can be tested without a Flutter binding.
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
/// the rest. The order is the engine's relevance/id order; re-sorting it would
/// make the same query look different from one page to the next.
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
