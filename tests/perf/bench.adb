with Ada.Command_Line;      use Ada.Command_Line;
with Ada.Text_IO;          use Ada.Text_IO;
with Ada.Streams.Stream_IO;
with Ada.Streams;          use Ada.Streams;
with Ada.Real_Time;        use Ada.Real_Time;
with Ada.Directories;

procedure Bench is
   package SIO renames Ada.Streams.Stream_IO;
   type SEA_Access is access Stream_Element_Array;
   type Id_Array is array (Natural range <>) of Long_Long_Integer;
   type Id_Access is access Id_Array;
   LF   : constant Stream_Element := Character'Pos (ASCII.LF);
   Zero : constant Stream_Element := Character'Pos ('0');
   Nine : constant Stream_Element := Character'Pos ('9');
   K    : constant := 7;

   function Slurp (Path : String; Len : out Stream_Element_Offset) return SEA_Access is
      Size : constant Stream_Element_Offset :=
        Stream_Element_Offset (Ada.Directories.Size (Path));
      F    : SIO.File_Type;
      Buf  : constant SEA_Access := new Stream_Element_Array (1 .. Size);
      Last : Stream_Element_Offset;
   begin
      SIO.Open (F, SIO.In_File, Path);
      SIO.Read (F, Buf.all, Last);
      SIO.Close (F);
      Len := Last;
      return Buf;
   end Slurp;

   function Count_Lines (S : Stream_Element_Array; Last : Stream_Element_Offset;
                         Pat : Stream_Element_Array) return Natural is
      Hits : Natural := 0;
      Line : Boolean := False;
      PL   : constant Stream_Element_Offset := Pat'Length;
      P0   : constant Stream_Element := Pat (Pat'First);
   begin
      for I in 1 .. Last loop
         declare B : constant Stream_Element := S (I); begin
            if B = LF then
               if Line then Hits := Hits + 1; end if;
               Line := False;
            elsif (not Line) and then B = P0 and then I + PL - 1 <= Last then
               declare Match : Boolean := True; begin
                  for J in 1 .. PL - 1 loop
                     if S (I + J) /= Pat (Pat'First + J) then Match := False; exit; end if;
                  end loop;
                  if Match then Line := True; end if;
               end;
            end if;
         end;
      end loop;
      if Line then Hits := Hits + 1; end if;
      return Hits;
   end Count_Lines;

   function Read_Postings (Path : String; N : out Natural) return Id_Access is
      Len   : Stream_Element_Offset;
      D     : constant SEA_Access := Slurp (Path, Len);
      Lines : Natural := 0;
   begin
      for I in 1 .. Len loop if D (I) = LF then Lines := Lines + 1; end if; end loop;
      declare
         A   : constant Id_Access := new Id_Array (0 .. Integer'Max (Lines, 1) - 1);
         Kk  : Natural := 0;
         Cur : Long_Long_Integer := 0;
         Any : Boolean := False;
      begin
         for I in 1 .. Len loop
            declare B : constant Stream_Element := D (I); begin
               if B = LF then
                  if Any then A (Kk) := Cur; Kk := Kk + 1; end if;
                  Cur := 0; Any := False;
               elsif B >= Zero and then B <= Nine then
                  Cur := Cur * 10 + Long_Long_Integer (B - Zero); Any := True;
               end if;
            end;
         end loop;
         N := Kk;
         return A;
      end;
   end Read_Postings;

   function Intersect (A : Id_Array; NA : Natural; B : Id_Array; NB : Natural) return Natural is
      I : Natural := 0; J : Natural := 0; C : Natural := 0;
   begin
      while I < NA and then J < NB loop
         if A (I) = B (J) then C := C + 1; I := I + 1; J := J + 1;
         elsif A (I) < B (J) then I := I + 1;
         else J := J + 1;
         end if;
      end loop;
      return C;
   end Intersect;

   procedure Row (Tag : String; It : Natural; T0 : Time; Extra : String) is
      US : constant Integer := Integer (Float (To_Duration (Clock - T0)) * 1_000_000.0);
   begin
      Put_Line (Tag & " iter" & Integer'Image (It) & " " &
                Integer'Image (US) & " us  " & Extra);
   end Row;
begin
   if Argument (1) = "find" then
      declare
         Len  : Stream_Element_Offset;
         S    : constant SEA_Access := Slurp (Argument (2), Len);
         PatS : constant String := Argument (3);
         Pat  : Stream_Element_Array (1 .. PatS'Length);
      begin
         for I in PatS'Range loop
            Pat (Stream_Element_Offset (I - PatS'First + 1)) := Character'Pos (PatS (I));
         end loop;
         for It in 0 .. K - 1 loop
            declare T0 : constant Time := Clock;
                    H  : constant Natural := Count_Lines (S.all, Len, Pat);
            begin Row ("find", It, T0, "hits=" & Integer'Image (H)); end;
         end loop;
      end;
   else
      declare
         NA, NB : Natural;
         A : constant Id_Access := Read_Postings (Argument (2), NA);
         B : constant Id_Access := Read_Postings (Argument (3), NB);
      begin
         for It in 0 .. K - 1 loop
            declare T0 : constant Time := Clock;
                    C  : constant Natural := Intersect (A.all, NA, B.all, NB);
            begin Row ("and ", It, T0, "common=" & Integer'Image (C)); end;
         end loop;
      end;
   end if;
end Bench;
