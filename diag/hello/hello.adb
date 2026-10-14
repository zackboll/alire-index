with Ada.Text_IO;
procedure Hello is
   Count : Natural := 0;
begin
   begin
      raise Constraint_Error with "probe";
   exception
      when Constraint_Error =>
         Count := Count + 1;
   end;
   Ada.Text_IO.Put_Line ("ADA HELLO OK exceptions=" & Count'Image);
end Hello;
