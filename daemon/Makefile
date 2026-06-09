# Resident POV-Ray render daemon — links against the vendored POV-Ray 3.7 archives.
POV   := ../vendor-povray
INC   := -I$(POV)/source -I$(POV)/source/backend -I$(POV)/source/base \
         -I$(POV)/source/frontend -I$(POV)/vfe -I$(POV)/vfe/unix -I$(POV)/unix
DEFS  := -DHAVE_CONFIG_H
CXXFLAGS := -O3 -ffast-math -march=native -pipe -Wno-multichar -Wno-write-strings \
            -fno-enforce-eh-specs -Wno-non-template-friend $(DEFS) $(INC)

# POV's own archives (start-group resolves the vfe<->povray circular refs)
ARCHIVES := -Wl,--start-group $(POV)/vfe/libvfe.a $(POV)/source/libpovray.a -Wl,--end-group
# mirror of unix/Makefile LIBS
LIBS := -lXpm -lSM -lICE -lX11 \
        -lOpenEXR-3_1 -lOpenEXRUtil-3_1 -lOpenEXRCore-3_1 -lIex-3_1 -lIlmThread-3_1 -lImath-3_1 \
        -ltiff -ljpeg -lpng -lz -lrt -lm -lboost_thread -lboost_system -lpthread

resident: resident.cpp $(POV)/vfe/libvfe.a $(POV)/source/libpovray.a
	$(CXX) $(CXXFLAGS) -std=c++11 -o $@ resident.cpp $(ARCHIVES) $(LIBS)

clean:
	rm -f resident *.ppm *.png
