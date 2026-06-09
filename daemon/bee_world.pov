// bee_world.pov — early-CGI meadow: gradient sky + clouds, grassy ground, simple
// trees, and a bee flying a looping path with flapping wings. Real-time in the
// Feverdream engine. clock drives the bee; CAMA/CAMR orbit the camera (live.cpp).
#version 3.7;
global_settings { assumed_gamma 1.0 }
#include "colors.inc"

#ifndef (CAMA) #declare CAMA = 25; #end
#ifndef (CAMR) #declare CAMR = 17; #end

// --- sky: horizon haze -> blue, with soft clouds ---
sky_sphere {
  pigment { gradient y
    color_map { [0.0 rgb <0.75,0.85,0.95>][0.25 rgb <0.45,0.65,0.92>][1.0 rgb <0.15,0.35,0.80>] }
    scale 2 translate y*-0.2 }
  pigment { bozo turbulence 0.5 scale <3,1,3>
    color_map { [0.0 rgbt <1,1,1,1>][0.55 rgbt <1,1,1,1>][0.75 rgbt <1,1,1,0.1>][1.0 rgbt <1,1,1,0.4>] }
    scale 6 }
}
light_source { <-30,40,-25> color rgb <1.0,0.97,0.88> }
light_source { <20,25,30> color rgb <0.3,0.35,0.45> shadowless }

// --- grassy ground: green with variation + a little roughness ---
plane { y, 0
  texture {
    pigment { granite color_map { [0 rgb <0.18,0.42,0.14>][0.6 rgb <0.26,0.55,0.18>][1 rgb <0.34,0.62,0.24>] } scale 3 }
    normal { bumps 0.35 scale 0.4 }
    finish { ambient 0.4 diffuse 0.7 specular 0 }
  }
}

// --- a simple early-CGI tree: trunk + foliage (pine cone or lollipop) ---
#macro Tree(P, s, kind)
  union {
    cylinder { <0,0,0>, <0,2.4,0>, 0.28 pigment { rgb <0.42,0.27,0.13> } finish { ambient 0.35 } }
    #if (kind = 0)   // pine: stacked cones
      cone { <0,1.8,0>,1.6, <0,3.6,0>,0   pigment { rgb <0.12,0.45,0.16> } finish { ambient 0.35 } }
      cone { <0,3.0,0>,1.1, <0,4.6,0>,0   pigment { rgb <0.15,0.50,0.18> } finish { ambient 0.35 } }
    #else            // lollipop: sphere foliage
      sphere { <0,3.6,0>,1.7 pigment { rgb <0.16,0.52,0.20> } finish { ambient 0.35 } }
      sphere { <0.9,3.0,0.4>,1.1 pigment { rgb <0.14,0.48,0.18> } }
    #end
    scale s translate P
  }
#end

// scatter trees deterministically around the meadow edge
#declare RS = seed(1942);
#declare i = 0;
#while (i < 11)
  #local ang = i*0.571 + rand(RS)*0.3;
  #local rad = 13 + rand(RS)*9;
  Tree(<sin(ang)*rad, 0, cos(ang)*rad>, 0.8+rand(RS)*0.7, (rand(RS) > 0.5))
  #declare i = i+1;
#end

// foreground grass tufts for detail
#declare j = 0;
#while (j < 60)
  #local gx = -10 + rand(RS)*20;  #local gz = -12 + rand(RS)*16;
  cone { <gx,0,gz>,0.06, <gx+rand(RS)*0.2-0.1,0.5+rand(RS)*0.4,gz+rand(RS)*0.2-0.1>,0
         pigment { rgb <0.2,0.5+rand(RS)*0.2,0.16> } }
  #declare j = j+1;
#end

// --- the bee ---
#macro Bee(P, faceDeg, wf)
  union {
    // abdomen: yellow/black stripes via a banded gradient
    sphere { 0,1 scale <0.55,0.5,0.95> translate <0,0,0.5>
      pigment { gradient z scale 0.36
        color_map { [0.0 rgb <1,0.78,0>][0.45 rgb <1,0.78,0>][0.5 rgb <0.07,0.06,0.04>]
                    [0.95 rgb <0.07,0.06,0.04>][1.0 rgb <1,0.78,0>] } }
      finish { phong 0.5 ambient 0.4 } }
    // thorax (fuzzy brown) + head
    sphere { <0,0,-0.35>,0.5 pigment { rgb <0.35,0.22,0.06> } finish { ambient 0.4 } }
    sphere { <0,0.04,-0.95>,0.38 pigment { rgb <0.08,0.08,0.08> } finish { ambient 0.4 } }
    sphere { <-0.22,0.08,-1.12>,0.13 pigment { rgb <0.02,0.02,0.02> } finish { phong 1 } }
    sphere { < 0.22,0.08,-1.12>,0.13 pigment { rgb <0.02,0.02,0.02> } finish { phong 1 } }
    // antennae
    cylinder { <-0.1,0.25,-1.05>,<-0.22,0.7,-1.35>,0.025 pigment { rgb 0.05 } }
    cylinder { < 0.1,0.25,-1.05>,< 0.22,0.7,-1.35>,0.025 pigment { rgb 0.05 } }
    // stinger
    cone { <0,0,1.35>,0.09,<0,0,1.7>,0 pigment { rgb <0.1,0.08,0.05> } }
    // wings (flap by wf) — translucent, hinged at the thorax
    #local wa = sin(wf)*40;
    sphere { 0,1 scale <0.12,0.03,0.7> translate <0.55,0.05,-0.1>
      rotate z*(18+wa) translate <0.05,0.35,-0.2>
      pigment { rgbt <0.85,0.92,1,0.62> } finish { phong 1 specular 0.5 } }
    sphere { 0,1 scale <0.12,0.03,0.7> translate <-0.55,0.05,-0.1>
      rotate z*(-18-wa) translate <-0.05,0.35,-0.2>
      pigment { rgbt <0.85,0.92,1,0.62> } finish { phong 1 specular 0.5 } }
    rotate y*faceDeg translate P
  }
#end

// flight path: a loop over the meadow, gentle bob, fast wing flap
#declare bt = clock;
#declare bx = sin(bt*2*pi)*7;
#declare bz = cos(bt*2*pi)*7 - 2;
#declare by = 2.6 + sin(bt*4*pi)*0.7;
#declare bface = degrees(bt*2*pi) - 90;   // face along the velocity tangent (nose-first)
Bee(<bx,by,bz>, bface, bt*240)

// --- camera: orbit the meadow (arrows in live.cpp) ---
#declare camx = sin(radians(CAMA))*CAMR;
#declare camz = cos(radians(CAMA))*CAMR;
camera { location <camx, 6.5, camz> look_at <0,2.4,-2> angle 52 right x*16/9 up y }
