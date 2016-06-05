# 
# 1. General Compiler Settings
#
CXX       = g++
CXXFLAGS  = -std=c++11 -Wall -Wextra -Wcast-qual -fno-exceptions -fno-rtti \
            -pedantic -Wno-long-long -msse4.2 -D__STDC_CONSTANT_MACROS -fopenmp
INCLUDES  =
LIBRARIES = -lpthread

#
# 2. Target Specific Settings
#
ifeq ($(TARGET),gikou)        # Windowsの実行ファイル
	sources  := $(shell ls src/*.cc)
	CXXFLAGS += -O3 -DNDEBUG -DMINIMUM -DPSEUDO_RANDOM_DEVICE -static
endif
ifeq ($(TARGET),release)      # Mac / Linuxの実行ファイル
	sources  := $(shell ls src/*.cc)
	CXXFLAGS += -O3 -DNDEBUG
endif
ifeq ($(TARGET),cluster)      # 疎結合並列探索のマスター側（デバッグ用、assertマクロON）
	sources  := $(shell ls src/*.cc)
	CXXFLAGS += -O3 -DCLUSTER
endif
ifeq ($(TARGET),consultation) # 合議アルゴリズムのマスター側（デバッグ用、assertマクロON）
	sources  := $(shell ls src/*.cc)
	CXXFLAGS += -O3 -DCONSULTATION
endif
ifeq ($(TARGET),development)  # 開発用・デバッグ用
	sources  := $(shell ls src/*.cc)
	CXXFLAGS += -O2 -g3
endif
ifeq ($(TARGET),profile)      # プロファイル用
	sources  := $(shell ls src/*.cc)
	CXXFLAGS += -O3 -DNDEBUG -pg
endif
ifeq ($(TARGET),test)         # ユニットテスト用（Google Testを利用）
	sources  := $(shell ls src/*.cc test/*.cc test/common/*.cc)
	sources  += lib/gtest-1.7.0/fused-src/gtest/gtest-all.cc
	CXXFLAGS += -g3 -Og -DUNIT_TEST
	INCLUDES += -Isrc -Ilib/gtest-1.7.0/fused-src
endif
ifeq ($(TARGET),coverage)     # カバレッジテスト用（Google Testを利用）
	sources  := $(shell ls src/*.cc test/*.cc test/common/*.cc)
	sources  += lib/gtest-1.7.0/fused-src/gtest/gtest-all.cc
	CXXFLAGS += -g3 -DUNIT_TEST -ftest-coverage -fprofile-arcs
	INCLUDES += -Isrc -Ilib/gtest-1.7.0/fused-src
endif

#
# 3. Default Settings (applied if there is no target-specific settings)
#
output_file  ?= bin/$(TARGET)
object_dir   ?= obj/$(TARGET)
objects      ?= $(sources:%.cc=$(object_dir)/%.o)
dependencies ?= $(objects:%.o=%.d)
directories  ?= $(sort $(dir $(objects))) bin

#
# 4. Public Targets
#
.PHONY: gikou release cluster consultation development profile test coverage run-coverage clean scaffold

gikou release cluster consultation development profile test coverage:
	$(MAKE) TARGET=$@ executable

run-coverage: coverage
	bin/coverage --gtest_output=xml

clean:
	rm -rf obj/*

scaffold:
	mkdir -p src test bin/data doc lib obj resource

#
# 5. Private Targets
#
.PHONY: executable
executable: $(directories) $(objects)
	$(CXX) $(CXXFLAGS) -o $(output_file) $(objects) $(LIBRARIES)
	
$(directories):
	mkdir -p $@

$(object_dir)/%.o: %.cc
	$(CXX) -c -MMD -MP -o $@ $(CXXFLAGS) $(INCLUDES) $<

-include $(dependencies)