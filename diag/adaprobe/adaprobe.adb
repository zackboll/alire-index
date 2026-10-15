--  IMGPROC-ALIRE-003 disposable probe: prints the opencv_imgproc 0.2.0
--  Extract_Subpixel_Patch results for a fixed case list in exact hex, in the
--  same format as `probe2 canonical`, for byte-for-byte comparison.
with Ada.Strings.Fixed;
with Ada.Text_IO;
with Ada.Unchecked_Conversion;
with Interfaces;
with OpenCV;
with OpenCV.Core;
with OpenCV.Core.Float32_Access;
with OpenCV.Core.UInt8_Access;
with OpenCV.Image_Processing;

procedure Adaprobe is
   package C renames OpenCV.Core;
   package IP renames OpenCV.Image_Processing;
   use type Interfaces.Unsigned_32;

   function Bits is new
     Ada.Unchecked_Conversion (OpenCV.Float32_Value, Interfaces.Unsigned_32);

   Hex_Digits : constant String := "0123456789ABCDEF";

   function Hex (V : Interfaces.Unsigned_32; Width : Positive) return String is
      Result : String (1 .. Width);
      X      : Interfaces.Unsigned_32 := V;
   begin
      for I in reverse Result'Range loop
         Result (I) := Hex_Digits (Integer (X and 16#F#) + 1);
         X := Interfaces.Shift_Right (X, 4);
      end loop;
      return Result;
   end Hex;

   function Img (N : Integer) return String
   is (Ada.Strings.Fixed.Trim (N'Image, Ada.Strings.Both));

   type Combo is (U8_U8, U8_F32, F32_F32);

   function Name (K : Combo) return String
   is (case K is
         when U8_U8   => "u8>u8",
         when U8_F32  => "u8>f32",
         when F32_F32 => "f32>f32");

   function Ramp (Float_Source : Boolean) return C.Mat is
      M : C.Mat :=
        C.Create
          (4, 4, (if Float_Source then (C.Float32, 1) else (C.UInt8, 1)));
   begin
      for R in 0 .. 3 loop
         for Col in 0 .. 3 loop
            if Float_Source then
               C.Float32_Access.Set
                 (M, R, Col, OpenCV.Float32_Value (R * 40 + Col * 8));
            else
               C.UInt8_Access.Set
                 (M, R, Col, OpenCV.UInt8_Value (R * 40 + Col * 8));
            end if;
         end loop;
      end loop;
      return M;
   end Ramp;

   subtype Coord_Text is String (1 .. 4);
   CX  : constant array (1 .. 9) of OpenCV.Float32_Value :=
     [3.0, 0.0, 3.0, 0.0, 1.0, 2.75, 3.0, 2.5, 1.25];
   CY  : constant array (1 .. 9) of OpenCV.Float32_Value :=
     [0.0, 0.0, 3.0, 3.0, 1.0, 0.25, 0.5, 0.0, 2.75];
   CXS : constant array (1 .. 9) of Coord_Text :=
     ["3.00", "0.00", "3.00", "0.00", "1.00", "2.75", "3.00", "2.50", "1.25"];
   CYS : constant array (1 .. 9) of Coord_Text :=
     ["0.00", "0.00", "3.00", "3.00", "1.00", "0.25", "0.50", "0.00", "2.75"];
   type Size_Pair is array (1 .. 2) of Positive;
   Sizes : constant array (1 .. 4) of Size_Pair :=
     [[3, 3], [2, 2], [5, 5], [1, 1]];
begin
   for K in Combo loop
      declare
         Source : constant C.Mat := Ramp (Float_Source => K = F32_F32);
         Depth  : constant IP.Subpixel_Patch_Output_Depth :=
           (if K = U8_F32
            then IP.Promote_To_Float32
            else IP.Preserve_Source_Depth);
      begin
         for S of Sizes loop
            for I in CX'Range loop
               declare
                  W     : constant Positive := S (1);
                  H     : constant Positive := S (2);
                  Patch : constant C.Mat :=
                    IP.Extract_Subpixel_Patch
                      (Source,
                       (OpenCV.Size_Coordinate (W), OpenCV.Size_Coordinate (H)),
                       (CX (I), CY (I)),
                       Depth);
               begin
                  Ada.Text_IO.Put
                    ("CANON " & Name (K) & " " & Img (W) & "x" & Img (H) & " "
                     & CXS (I) & "," & CYS (I) & ":");
                  for R in 0 .. H - 1 loop
                     for Col in 0 .. W - 1 loop
                        if K = U8_U8 then
                           Ada.Text_IO.Put
                             (" "
                              & Hex
                                  (Interfaces.Unsigned_32
                                     (C.UInt8_Access.Get (Patch, R, Col)),
                                   2));
                        else
                           Ada.Text_IO.Put
                             (" " & Hex (Bits (C.Float32_Access.Get (Patch, R, Col)), 8));
                        end if;
                     end loop;
                  end loop;
                  Ada.Text_IO.New_Line;
               end;
            end loop;
         end loop;
      end;
   end loop;
end Adaprobe;
