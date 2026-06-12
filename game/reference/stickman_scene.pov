// SPDX-License-Identifier: MIT
// stickman_scene.pov — the ORIGINAL Feverdream character, kept as reference.
//
// Before Chunkins the squirrel, this little fellow was the player: a torso
// sphere, a head, and two legs that swing about the hips by STEP. He shipped
// in the first playable build (fd-game commit a07810e) and in walk.cpp before
// that. He's preserved here as a worked example of a declares-driven
// character: the host feeds POSX/POSZ/JUMP/TURN/STEP as plain name=float
// declares every frame, and all motion falls out of the scene math.
//
// Render him standalone with stock POV-Ray:
//   povray stickman_scene.pov +W640 +H360 Declare=STEP=1.2
// or through the resident daemon (see ../../daemon/fd_client.py):
//   c.scene(open("stickman_scene.pov").read())
//   c.render(320, 180, declares={"STEP": 1.2, "TURN": 30})
//
// To put him back in the game, replace the CHUNKINS union in
// fd-game.cpp build_scene() with the union below — the declares contract
// (POSX/POSZ/JUMP/TURN/STEP) is identical. That's the whole point: the
// character is just scene text; the engine doesn't care who the hero is.

#version 3.7;
#ifndef (POSX) #declare POSX=0; #end
#ifndef (POSZ) #declare POSZ=0; #end
#ifndef (JUMP) #declare JUMP=0; #end
#ifndef (TURN) #declare TURN=0; #end   // degrees
#ifndef (STEP) #declare STEP=0; #end   // walk-cycle phase (radians-ish)

global_settings { assumed_gamma 1.0 }
background { rgb <0.55,0.70,0.90> }
light_source { <-14,18,-10> rgb 1 }
light_source { <10,8,6> rgb <0.25,0.25,0.4> shadowless }
plane { y,0 pigment { checker rgb 0.78 rgb 0.28 } finish { ambient 0.35 } }
camera { location <POSX-sin(radians(TURN))*6, 3.2+JUMP*0.85,
                   POSZ-cos(radians(TURN))*6>
  look_at <POSX, 1.2+JUMP*0.85, POSZ> angle 50 right x*16/9 up y }

// ---- the original little man, verbatim from the first playable build ----
// torso + head, legs swing about the hip by STEP
#declare LEG = 28*sin(STEP);
union {
  sphere { <0,1.15,0>, 0.42 scale <0.8,1.15,0.6>
    pigment { rgb <0.85,0.45,0.2> } finish { phong 0.7 } }
  sphere { <0,1.95,0>, 0.26 pigment { rgb <0.95,0.8,0.65> } finish { phong 0.7 } }
  box { <-0.10,-0.85,-0.10>,<0.10,0,0.10> rotate x*LEG  translate <-0.16,0.85,0>
    pigment { rgb <0.25,0.3,0.6> } finish { phong 0.5 } }
  box { <-0.10,-0.85,-0.10>,<0.10,0,0.10> rotate x*-LEG translate < 0.16,0.85,0>
    pigment { rgb <0.25,0.3,0.6> } finish { phong 0.5 } }
  rotate y*TURN translate <POSX,JUMP,POSZ>
}

// Historical note: the torso's `scale` above acts about the ORIGIN, so it
// also scales the sphere's position (1.15 -> 1.32). Here that accident made
// him taller and happened to look right. The same idiom later floated
// Chunkins' ears half a unit above his head and turned the first acorn cap
// into a belt — which is why the modern characters build at the origin,
// scale, THEN translate. Learn from the little man's lucky escape.
