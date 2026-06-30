import java.nio.file.*;
import java.nio.charset.StandardCharsets;

// Deliberately terse. Two ops the C engine does:
//   find <store> <substr>   full scan of the store, count records that match
//   and  <post1> <post2>    intersect two sorted posting lists (the AND of keys)
// Byte-level, no per-line String allocation, whole file pulled from page cache
// (warm) so we measure CPU+RAM like C's warm scan, not the disk.
public class Bench {
  static final int K = 7;                       // iterations: iter 0 = cold (JIT), rest warm

  public static void main(String[] a) throws Exception {
    if (a[0].equals("find")) {
      byte[] s = Files.readAllBytes(Paths.get(a[1]));
      byte[] p = a[2].getBytes(StandardCharsets.UTF_8);
      for (int it = 0; it < K; it++) {
        long t0 = System.nanoTime();
        int hits = countMatchingLines(s, p);
        System.out.printf("find  iter%d  %5.0f ms  hits=%d%n", it, (System.nanoTime()-t0)/1e6, hits);
      }
    } else { // and
      long[] A = readPostings(a[1]), B = readPostings(a[2]);
      for (int it = 0; it < K; it++) {
        long t0 = System.nanoTime();
        int c = intersect(A, B);
        System.out.printf("and   iter%d  %6.1f ms  common=%d  |A|=%d |B|=%d%n",
                          it, (System.nanoTime()-t0)/1e6, c, A.length, B.length);
      }
    }
  }

  static int countMatchingLines(byte[] s, byte[] p) {
    int hits = 0, pl = p.length, n = s.length; boolean line = false; byte p0 = p[0];
    for (int i = 0; i < n; i++) {
      byte b = s[i];
      if (b == '\n') { if (line) hits++; line = false; }
      else if (!line && b == p0 && i + pl <= n) {
        int j = 1; while (j < pl && s[i+j] == p[j]) j++;
        if (j == pl) line = true;
      }
    }
    if (line) hits++;
    return hits;
  }

  static long[] readPostings(String path) throws Exception {
    byte[] d = Files.readAllBytes(Paths.get(path));
    int lines = 0; for (byte b : d) if (b == '\n') lines++;
    long[] out = new long[lines]; int k = 0; long cur = 0; boolean any = false;
    for (byte b : d) {
      if (b == '\n') { if (any) out[k++] = cur; cur = 0; any = false; }
      else if (b >= '0' && b <= '9') { cur = cur*10 + (b - '0'); any = true; }
    }
    if (k != out.length) { long[] t = new long[k]; System.arraycopy(out,0,t,0,k); out = t; }
    return out; // idx posting lists are already sorted ascending
  }

  static int intersect(long[] A, long[] B) {
    int i=0,j=0,c=0;
    while (i<A.length && j<B.length) {
      long x=A[i], y=B[j];
      if (x==y){c++;i++;j++;} else if (x<y) i++; else j++;
    }
    return c;
  }
}
