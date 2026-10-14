with Ada.Text_IO;
with Interfaces.C;
procedure Mixed is
   use type Interfaces.C.int;
   function Cxx_Probe (N : Interfaces.C.int) return Interfaces.C.int
     with Import, Convention => C, External_Name => "cxx_probe";
   R : constant Interfaces.C.int := Cxx_Probe (3);
begin
   if R /= 8 then
      raise Program_Error with "unexpected C++ result" & R'Image;
   end if;
   begin
      raise Constraint_Error;
   exception
      when Constraint_Error => null;
   end;
   Ada.Text_IO.Put_Line ("ADA+C++ MIXED OK result=" & R'Image);
end Mixed;
