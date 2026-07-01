pragma SPARK_Mode (On);
package body Merge is
   function Intersect (A, B : Id_Array) return Natural is
      I : Positive := A'First;
      J : Positive := B'First;
      C : Natural  := 0;
   begin
      while I <= A'Last and then J <= B'Last loop
         pragma Loop_Invariant (I in A'First .. A'Last);
         pragma Loop_Invariant (J in B'First .. B'Last);
         pragma Loop_Invariant (C <= I - A'First and then C <= J - B'First);
         pragma Loop_Variant (Decreases => Long_Long_Integer (A'Last - I) + Long_Long_Integer (B'Last - J));
         if A (I) = B (J) then
            C := C + 1;
            I := I + 1;
            J := J + 1;
         elsif A (I) < B (J) then
            I := I + 1;
         else
            J := J + 1;
         end if;
      end loop;
      return C;
   end Intersect;
end Merge;
