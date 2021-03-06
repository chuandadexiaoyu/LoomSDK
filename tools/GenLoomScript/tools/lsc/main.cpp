/*
 * ===========================================================================
 * Loom SDK
 * Copyright 2011, 2012, 2013
 * The Game Engine Company, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ===========================================================================
 */

// LoomScript Compiler
#include "loom/common/core/log.h"
#include "loom/common/core/assert.h"
#include "loom/common/platform/platform.h"
#include "loom/common/platform/platformTime.h"
#include "loom/script/compiler/lsCompiler.h"
#include "loom/script/runtime/lsLuaState.h"
#include "loom/script/native/lsNativeDelegate.h"

using namespace LS;

void installPackageSystem();
void installPackageCompiler();

// we'll be able to do this from a unit test project post CLI sprint
// for now, old-sk001 beats!

extern "C" {
void stringtable_initialize();
#ifdef LOOM_EMBEDDED_SYSTEMLIB
extern const long int system_loomlib_size;
extern const unsigned char system_loomlib[];
#endif

}

#if LOOM_PLATFORM == LOOM_PLATFORM_OSX
#include <unistd.h>
#include <mach-o/dyld.h> /* _NSGetExecutablePath */

utString GetLSCPath()
{
    char         path[4096];
    unsigned int size = sizeof(path);

    if (_NSGetExecutablePath(path, &size) != 0)
    {
        lmAssert(0, "Error getting executable path");
    }

    return utString(path);
}


#elif LOOM_PLATFORM == LOOM_PLATFORM_WIN32

#include <windows.h>

utString GetLSCPath()
{
    char         path[4096];
    unsigned int size = sizeof(path);

    GetModuleFileName(NULL, path, size);

    return utString(path);
}


#else

#include <unistd.h>

utString GetLSCPath()
{
    char buffer[BUFSIZ];

    readlink("/proc/self/exe", buffer, BUFSIZ);
    return utString(buffer);
}
#endif


void RunUnitTests()
{
    LSCompiler::setRootBuildFile("Tests.build");
    LSCompiler::initialize();

    LSLuaState *testVM = new LSLuaState();

    // FIXME:  default paths
    testVM->open();

    Assembly *testAssembly = testVM->loadExecutableAssembly("Tests.loom");
    testAssembly->execute();

    testVM->close();
}


void RunBenchmarks()
{
    LSCompiler::setRootBuildFile("Benchmarks.build");
    LSCompiler::initialize();

    LSLuaState *benchVM = new LSLuaState();

    benchVM->open();

    Assembly *benchAssembly = benchVM->loadExecutableAssembly("Benchmarks.loom");
    benchAssembly->execute();

    benchVM->close();
}


int main(int argc, const char **argv)
{
    stringtable_initialize();
    loom_log_initialize();
    platform_timeInitialize();

    NativeDelegate::markMainThread();

#ifdef LOOM_EMBEDDED_SYSTEMLIB
    // ensure null terminated
    
    char* data = (char*) malloc(system_loomlib_size + 1);
    memset(data, 0, system_loomlib_size + 1);
    memcpy(data, system_loomlib, system_loomlib_size);
    LSCompiler::setEmbeddedSystemAssembly((const char*)data);
    
#endif    
    

    LSLuaState::initCommandLine(argc, argv);

#ifdef LOOM_ENABLE_JIT
    printf("LSC - JIT Compiler\n");
#else
    printf("LSC - Interpreted Compiler\n");
#endif

    bool runtests      = false;
    bool runbenchmarks = false;
    bool symbols       = false;

    const char *rootBuildFile = NULL;

    for (int i = 1; i < argc; i++)
    {
        if ((strlen(argv[i]) >= 2) && (argv[i][0] == '-') && (argv[i][1] == 'D'))
        {
            continue;
        }
        else if (!strcmp(argv[i], "--release"))
        {
            LSCompiler::setDebugBuild(false);
        }
        else if (!strcmp(argv[i], "--verbose"))
        {
            LSCompiler::setVerboseLog(true);
        }
        else if (!strcmp(argv[i], "--unittest"))
        {
            runtests = true;
        }
        else if (!strcmp(argv[i], "--benchmark"))
        {
            runbenchmarks = true;
        }
        else if (!strcmp(argv[i], "--symbols"))
        {
            symbols = true;
        }
        else if (!strcmp(argv[i], "--xmlfile"))
        {
            i++;      // skip the filename
            continue; // unit tests option
        }
        else if (!strcmp(argv[i], "--classpath") && i + 1 < argc)
        {
            i++;      // add the sourcepath
            LSCompiler::addSourcePath(argv[i]);
            continue;

        }
        else if (!strcmp(argv[i], "--root"))
        {
            i++;
            if (i >= argc)
            {
                LSError("--root option requires folder to be specified");
            }

            printf("Root set to %s\n", argv[i]);

#if LOOM_PLATFORM == LOOM_PLATFORM_OSX || LOOM_PLATFORM == LOOM_PLATFORM_LINUX
            chdir(argv[i]);
#elif LOOM_PLATFORM == LOOM_PLATFORM_WIN32
            ::SetCurrentDirectory(argv[i]);
#endif
        }
        else if (!strcmp(argv[i], "--help"))
        {
            printf("--release : build in release mode\n");
            printf("--verbose : enable verbose compilation\n");
            printf("--unittest [--xmlfile filename.xml]: run unit tests with optional xml file output\n");
            printf("--root: set the SDK root\n");
            printf("--symbols : dump symbols for binary executable\n");
            printf("--classpath [--classpath ../moresources]: add an additional source folder\n");
            printf("--help: display this help\n");
            return EXIT_SUCCESS;
        }
        else if (strstr(argv[i], ".build"))
        {
            rootBuildFile = argv[i];
        }
        else
        {
            printf("unknown option: %s\n", argv[i]);
            printf("lsc --help for a list of options\n");
            return EXIT_FAILURE;
        }
    }

    installPackageSystem();
    installPackageCompiler();

    LSCompiler::processLoomConfig();

    if (runtests)
    {
        RunUnitTests();
        return EXIT_SUCCESS;
    }

    if (runbenchmarks)
    {
        RunBenchmarks();
        return EXIT_SUCCESS;
    }

    LSCompiler::setDumpSymbols(symbols);

    // todo, better sdk detection
    // TODO: LOOM-690 - find a better paradigm here.
    utString lscpath = GetLSCPath();
    if (strstr(lscpath.c_str(), "loom/sdks/") || strstr(lscpath.c_str(), "loom\\sdks\\"))
    {
        LSCompiler::setSDKBuild(lscpath);
    }

    if (!rootBuildFile)
    {
        printf("Building Main.loom with default settings\n");
        LSCompiler::defaultRootBuildFile();
    }
    else
    {
        printf("Building %s\n", rootBuildFile);
        LSCompiler::setRootBuildFile(rootBuildFile);
    }


    LSCompiler::initialize();

    return EXIT_SUCCESS;
}
