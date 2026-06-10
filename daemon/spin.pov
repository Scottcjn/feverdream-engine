// SPDX-License-Identifier: MIT
#version 3.7;
#include "colors.inc"
// camera orbit controllable from the host (CAMA=azimuth deg, CAMR=radius)
#ifndef (CAMA) #declare CAMA = 0; #end
#ifndef (CAMR) #declare CAMR = 9; #end
#declare camx =  sin(radians(CAMA)) * CAMR;
#declare camz = -cos(radians(CAMA)) * CAMR;
background { rgb <0.05,0.05,0.10> }
light_source { <-8,12,-10> rgb 1 }
light_source { <8,6,-4> rgb <0.3,0.3,0.45> shadowless }
camera { location <camx,4.5,camz> look_at <0,0.6,0> angle 48 right x*16/9 up y }
plane { y,0 pigment { checker rgb 0.85 rgb 0.30 } finish { ambient 0.4 } }
#declare i=0;
#while (i<7)
  sphere { <sin(i*0.9+clock*6.283)*3, 0.7, cos(i*0.9+clock*6.283)*3>, 0.55
    pigment { rgb <0.6,0.3+i*0.09,0.95> } finish { phong 0.8 reflection 0.25 } }
  #declare i=i+1;
#end
torus { 2.6, 0.18 rotate x*90 translate y*0.2 pigment { rgb <1,0.7,0.2> } finish { phong 1 reflection 0.3 } }
