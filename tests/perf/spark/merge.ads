pragma SPARK_Mode (On);
package Merge is
   type Id_Array is array (Positive range <>) of Long_Long_Integer;
   --  Count values common to two sorted posting lists. This unit proves ABSENCE
   --  OF RUNTIME ERRORS: gnatprove shows every array index is in range, no
   --  arithmetic overflows, and the loop terminates -- so the -gnatp (checks-off,
   --  C-speed) build is safe by proof, not by luck.
   function Intersect (A, B : Id_Array) return Natural with
     Pre => A'Length > 0 and then B'Length > 0
            and then A'Last < Positive'Last and then B'Last < Positive'Last;
end Merge;
