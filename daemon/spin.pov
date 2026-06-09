#version 3.7;
#include "colors.inc"
background { rgb <0.05,0.05,0.10> }
light_source { <-8,12,-10> rgb 1 }
camera { location <0,4,-8> look_at <0,0.5,0> angle 45 right x*16/9 up y }
plane { y,0 pigment { checker rgb 0.85 rgb 0.30 } finish { ambient 0.4 } }
#declare i=0;
#while (i<7)
  sphere { <sin(i*0.9+clock*6.283)*3, 0.7, cos(i*0.9+clock*6.283)*3>, 0.55
    pigment { rgb <0.6,0.3+i*0.09,0.95> } finish { phong 0.8 reflection 0.2 } }
  #declare i=i+1;
#end
