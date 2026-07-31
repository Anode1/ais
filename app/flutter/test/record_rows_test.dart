// Regression: a recall page must show ONE ROW PER RECORD, not one per link.
//
// There is no "delete one link" operation, so a per-link row meant deleting any
// of them destroyed the whole record, and the delete path removed the first row
// carrying that id -- usually not the row the user tapped. Three acceptance
// testers hit it; the app removed a row they had not touched and left the one
// they deleted on screen.
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/record_rows.dart';

void main() {
  group('oneRowPerRecord', () {
    test('a multi-link record becomes ONE row', () {
      final g = oneRowPerRecord(const [
        Hit(1, 'https://x/a'),
        Hit(1, 'https://x/b'),
        Hit(1, 'https://x/c'),
      ]);
      expect(g.rows.length, 1);
      expect(g.rows.first.value, 'https://x/a'); // the first link represents it
      expect(g.extra[1], 2); // and the row can say there are two more
    });

    test('single-link records are untouched', () {
      final g = oneRowPerRecord(const [Hit(1, 'a'), Hit(2, 'b'), Hit(3, 'c')]);
      expect(g.rows.length, 3);
      expect(g.extra.isEmpty, true);
    });

    test('a mixed page keeps every record exactly once', () {
      final g = oneRowPerRecord(const [
        Hit(1, 'a1'),
        Hit(2, 'b1'),
        Hit(2, 'b2'),
        Hit(3, 'c1'),
        Hit(2, 'b3'),
      ]);
      expect(g.rows.map((h) => h.id).toList(), [1, 2, 3]);
      expect(g.extra[2], 2);
      expect(g.extra.containsKey(1), false);
    });

    test('engine order is preserved', () {
      // The order is the engine's; re-sorting would make the same query look
      // different from one page to the next.
      final g = oneRowPerRecord(const [Hit(9, 'x'), Hit(4, 'y'), Hit(7, 'z')]);
      expect(g.rows.map((h) => h.id).toList(), [9, 4, 7]);
    });

    test('an empty page yields nothing, not a crash', () {
      final g = oneRowPerRecord(const []);
      expect(g.rows.isEmpty, true);
      expect(g.extra.isEmpty, true);
    });

    test('links that are not adjacent still collapse', () {
      // recall does not promise a record's links arrive together.
      final g = oneRowPerRecord(const [
        Hit(5, 'p'),
        Hit(6, 'q'),
        Hit(5, 'r'),
      ]);
      expect(g.rows.length, 2);
      expect(g.extra[5], 1);
    });
  });
}
