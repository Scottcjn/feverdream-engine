// SPDX-License-Identifier: MIT
// splash.pov — the CHUNKINS title screen, raytraced like everything else.
// The host spins the pedestal via the SPIN declare; SPACE starts the quest.
#version 3.7;
#ifndef (SPIN) #declare SPIN = 0; #end

global_settings { assumed_gamma 1.0 }
sky_sphere {
  pigment { gradient y color_map {
    [0.0 rgb <0.75,0.85,0.95>][0.25 rgb <0.45,0.65,0.92>][1.0 rgb <0.15,0.35,0.80>] }
    scale 2 translate y*-0.2 }
  pigment { bozo turbulence 0.5 scale <3,1,3> color_map {
    [0.0 rgbt <1,1,1,1>][0.55 rgbt <1,1,1,1>][0.75 rgbt <1,1,1,0.1>][1.0 rgbt <1,1,1,0.4>] }
    scale 6 }
}
light_source { <-30,40,-25> rgb <1.0,0.97,0.88> }
light_source { <20,25,30> rgb <0.3,0.35,0.45> shadowless }
plane { y,0 texture {
  pigment { granite color_map {
    [0 rgb <0.18,0.42,0.14>][0.6 rgb <0.26,0.55,0.18>][1 rgb <0.34,0.62,0.24>] } scale 3 }
  normal { bumps 0.35 scale 0.4 }
  finish { ambient 0.4 diffuse 0.7 specular 0 } } }

// a few trees framing the title
#macro Tree(P, ts, kind)
  union {
    cylinder { <0,0,0>, <0,2.4,0>, 0.28 pigment { rgb <0.42,0.27,0.13> } finish { ambient 0.35 } }
    #if (kind = 0)
      cone { <0,1.8,0>,1.6, <0,3.6,0>,0 pigment { rgb <0.12,0.45,0.16> } finish { ambient 0.35 } }
      cone { <0,3.0,0>,1.1, <0,4.6,0>,0 pigment { rgb <0.15,0.50,0.18> } finish { ambient 0.35 } }
    #else
      sphere { <0,3.6,0>,1.7 pigment { rgb <0.16,0.52,0.20> } finish { ambient 0.35 } }
    #end
    scale ts translate P }
#end
Tree(<-10, 0, 9>, 1.3, 0)
Tree(< 10, 0, 8>, 1.2, 1)
Tree(<-13, 0, 4>, 0.9, 1)
Tree(< 13, 0, 3>, 1.0, 0)

// THE TITLE — extruded gold, raytraced reflections and all
text { ttf "timrom.ttf" "CHUNKINS" 0.45, 0
  pigment { rgb <1.0,0.82,0.25> }
  finish { phong 0.9 reflection 0.18 }
  scale <1.42,1.42,1>
  translate <-4.45, 3.5, 0> }
text { ttf "timrom.ttf" "The Search for the Golden Acorn" 0.3, 0
  pigment { rgb <0.95,0.93,0.85> }
  finish { phong 0.5 }
  scale <0.52,0.52,1>
  translate <-4.3, 2.6, 0> }
text { ttf "timrom.ttf" "PRESS ANY KEY" 0.3, 0
  pigment { rgb <1,1,1> }
  finish { ambient 0.7 }
  scale <0.55,0.55,1>
  translate <-2.1, 0.35, -1.0> }

// Chunkins, posing stage left (facing the camera)
#declare FUR   = pigment { rgb <0.55,0.32,0.15> };
#declare CREAM = pigment { rgb <0.93,0.85,0.70> };
#declare DARK  = pigment { rgb <0.10,0.07,0.05> };
union {
  sphere { 0, 0.48 scale <0.9,1.0,0.85> translate <0,0.95,0> pigment {FUR} finish { phong 0.6 } }
  sphere { 0, 0.34 scale <0.75,0.85,0.6> translate <0,0.88,0.20> pigment {CREAM} finish { phong 0.5 } }
  sphere { <0,1.62,0.10>, 0.30 pigment {FUR} finish { phong 0.6 } }
  sphere { 0, 0.13 scale <1,0.8,1.1> translate <0,1.54,0.36> pigment {CREAM} finish { phong 0.5 } }
  sphere { <0,1.57,0.47>, 0.045 pigment {DARK} }
  sphere { <-0.12,1.72,0.305>, 0.068 pigment { rgb <0.97,0.97,0.95> } finish { phong 0.9 } }
  sphere { < 0.12,1.72,0.305>, 0.068 pigment { rgb <0.97,0.97,0.95> } finish { phong 0.9 } }
  sphere { <-0.115,1.72,0.355>, 0.034 pigment {DARK} }
  sphere { < 0.115,1.72,0.355>, 0.034 pigment {DARK} }
  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate <-0.15,1.90,0.04> pigment {FUR} }
  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate < 0.15,1.90,0.04> pigment {FUR} }
  // little front paws, held up in the classic nibble pose
  sphere { <-0.21,0.82,0.30>, 0.095 pigment {FUR} finish { phong 0.5 } }
  sphere { < 0.21,0.82,0.30>, 0.095 pigment {FUR} finish { phong 0.5 } }
  box { <-0.09,-0.55,-0.09>,<0.09,0,0.09> translate <-0.17,0.55,0> pigment {FUR} finish { phong 0.5 } }
  box { <-0.09,-0.55,-0.09>,<0.09,0,0.09> translate < 0.17,0.55,0> pigment {FUR} finish { phong 0.5 } }
  union {
    sphere { <0.10,0.42,-0.40>, 0.17 }
    sphere { <0.16,0.76,-0.55>, 0.23 }
    sphere { <0.18,1.12,-0.54>, 0.26 }
    sphere { <0.14,1.44,-0.40>, 0.20 }
    pigment { rgb <0.62,0.34,0.14> } finish { phong 0.55 }
  }
  rotate y*168 translate <-3.1, 0, -0.6>
  scale 1.15
}

// the Golden Acorn, giant, slowly turning on its pedestal
union {
  sphere { 0, 0.20 scale <1,1.15,1> translate <0,0.20,0>
    pigment { rgb <1.00,0.84,0.25> } finish { phong 0.9 reflection 0.10 } }
  sphere { 0, 0.21 scale <1,0.55,1> translate <0,0.36,0>
    pigment { rgb <0.42,0.26,0.12> } finish { phong 0.5 } }
  cylinder { <0,0.44,0>, <0,0.56,0>, 0.035 pigment { rgb <0.38,0.23,0.10> } }
  scale 4.2
  rotate y*SPIN
  translate <3.5, 0.1, -0.4>
}

camera { location <0, 2.6, -9.5> look_at <0, 2.1, 0> angle 50 right x*16/9 up y }
