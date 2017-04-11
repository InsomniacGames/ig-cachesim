#include "Precompiled.h"
#include "SymbolResolver.h"
#include "CacheSim/CacheSimData.h"
#include <sstream>
#include <unistd.h>

static bool ResolveSymbolViaObjdump(char** symbolNameOut, size_t* bufferSizeOut, const char* module, uintptr_t address)
{
    std::stringstream command;

    command << "objdump " << module << " ";
    command << std::showbase << std::hex;
    command << "-C -d --start-address=" << address;
    command << " --stop-address=" << (address + 1);

    FILE* fd = popen(command.str().c_str(), "r");
    if ( fd == nullptr )
    {
      qDebug() << "Cannot find function info with addr2line or objdump either.";
      pclose(fd);
      return false;
    }

    char* buffer = nullptr;
    size_t length;
    while (getline(&buffer, &length, fd) != -1)
    {
      char* symbolStart = strchr(buffer, '<');
      if ( symbolStart == nullptr )
      {
        continue;
      }
      symbolStart++; // Start /after/ the '<'
      char* symbolEnd = strrchr(symbolStart, '>');
      if ( symbolEnd == nullptr || symbolEnd < symbolStart )
      {
        continue;
      }

      *symbolEnd = '\0';
      symbolEnd = strpbrk(symbolStart, "+-");
      if ( symbolEnd )
      {
        *symbolEnd = '\0';
      }

      size_t symbolLength = strlen(symbolStart) + 1;
      if ( symbolLength > *bufferSizeOut )
      {
        *symbolNameOut = (char*)realloc(*symbolNameOut, symbolLength);
        *bufferSizeOut = symbolLength;
      }

      strcpy(*symbolNameOut, symbolStart);
      pclose(fd);
      free(buffer);
      return true;
    }
    pclose(fd);
    free(buffer);
    return false;
}

#define DebugBreak() asm volatile("int $3")
bool CacheSim::ResolveSymbols(const UnresolvedAddressData& input, QVector<ResolvedSymbol>* resolvedSymbolsOut, SymbolResolveProgressCallbackType reportProgress)
{
  struct ModuleFrames
  {
      const SerializedModuleEntry* m_Entry;
      QVector<uintptr_t> m_Frames;
  };

  QVector<ModuleFrames> moduleFrameList;
  for ( int i = 0; i < input.m_ModuleCount; i++ )
  {
      ModuleFrames frames;
      frames.m_Entry = &input.m_Modules[i];
      moduleFrameList.push_back(frames);
  }

  std::sort(moduleFrameList.begin(), moduleFrameList.end(), [](const ModuleFrames& l, const ModuleFrames& r) -> bool
    {
      return l.m_Entry->m_ImageBase < r.m_Entry->m_ImageBase;
    });


  int resolve_count = 0;
  int fail_count = 0;

  QSet<uintptr_t> ripLookup;  
  const auto& addRipToModuleFrames = [&](uintptr_t rip) -> void
  {
    if ( ripLookup.contains(rip) )
    {
      return;
    }
    ripLookup.insert(rip);

    for ( int i = 0; i < input.m_ModuleCount; i++ )
    {
      ModuleFrames& module = moduleFrameList[i];
      if ( rip >= module.m_Entry->m_ImageBase + module.m_Entry->m_ImageSegmentOffset)
      {
        if ( rip < (module.m_Entry->m_ImageBase + module.m_Entry->m_ImageSegmentOffset + module.m_Entry->m_SizeBytes) )
        {
          module.m_Frames.push_back(rip);
          break;
        }
        else
          continue;
      }
      else
      {
        qDebug() << "Failed to find module for address " << rip;
        fail_count++;
      }
    }
  };

  
  int total = 2 * (input.m_StackCount + input.m_NodeCount); // Two passes, once to sort the data and once to process it
  int completed = 0;

  // Sort instructions in stacks
  for (uint32_t i = 0; i < input.m_StackCount; ++i, ++completed)
  {
    if (uintptr_t rip = input.m_Stacks[i])
    {
      addRipToModuleFrames(rip);
    }

    if (0 == (completed & 0x400))
    {
      reportProgress(completed, total);
    }
  }

  // Sort any instructions used in leaf functions.
  for (uint32_t i = 0; i < input.m_NodeCount; ++i, ++completed)
  {
    addRipToModuleFrames(input.m_Nodes[i].m_Rip);

    if (0 == (completed & 0x400))
    {
      reportProgress(completed, total);
    }
  }

  reportProgress(completed, total);

  for ( int i = 0; i < moduleFrameList.count(); i++ )
  {
    if ( moduleFrameList[i].m_Frames.count() == 0 )
    {
        continue;
    }

    ptrdiff_t moduleIndex = moduleFrameList[i].m_Entry - input.m_Modules;

    // Figure out the actual file with the debug info. If there's anything in /usr/lib/debug, use that version instead
    // resolve any symlinks
    char resolvedBaseFilename[1024];
    if ( realpath(input.m_ModuleNames[moduleIndex].toLatin1().data(), resolvedBaseFilename) == nullptr )
    {
      strcpy(resolvedBaseFilename, input.m_ModuleNames[moduleIndex].toLatin1().data());
    }

    const char* debugLibBase = "/usr/lib/debug";
    std::stringstream symbolFilename;
    symbolFilename << debugLibBase;
    symbolFilename << resolvedBaseFilename;
    if ( access(symbolFilename.str().c_str(), R_OK) == -1 )
    {
      // Didn't find a special symbol library, so use the code library
      symbolFilename.str(input.m_ModuleNames[moduleIndex].toLatin1().data());
    }
    
    int frameIndex = 0;
    do
    {
      std::stringstream command;
      command << "addr2line -C -f -e " << symbolFilename.str() << " ";
      command << std::showbase << std::hex;
  
      uintptr_t imageBase = moduleFrameList[i].m_Entry->m_ImageBase;
      const int numFramesPerStep = 1000; // popen has an undocumented maximum string length. This ensures we don't exceed it
      const int maxFrameIndex = std::min(frameIndex + numFramesPerStep, moduleFrameList[i].m_Frames.count()); 

      for ( int j = frameIndex; j < maxFrameIndex; j++ )
      {
        command << (moduleFrameList[i].m_Frames[j] - imageBase) << " ";
      }
      FILE* fd = popen(command.str().c_str(), "r");
  
      if ( fd == nullptr )
      {
        qDebug() << "Cannot resolve symbols. addr2line not found.";
        pclose(fd);
        return false;
      }

      char* symbolBuffer = nullptr;
      size_t symbolBufferSize;
      while ( getline(&symbolBuffer, &symbolBufferSize, fd) != -1 )
      {
        if ( strcmp(symbolBuffer, "??\n") == 0 )
        {
          // Attempt to use objdump to get the data
          if ( !ResolveSymbolViaObjdump(&symbolBuffer, &symbolBufferSize, input.m_ModuleNames[moduleIndex].toLatin1().data(), moduleFrameList[i].m_Frames[frameIndex] - moduleFrameList[i].m_Entry->m_ImageBase))
          {
            int needed = snprintf(symbolBuffer, symbolBufferSize, "[0x%016lx in %s]", moduleFrameList[i].m_Frames[frameIndex], symbolFilename.str().c_str());
            if ( needed >= symbolBufferSize )
            {
              symbolBufferSize = needed + 1;
              symbolBuffer = (char*)realloc(symbolBuffer, symbolBufferSize);
              snprintf(symbolBuffer, symbolBufferSize, "[%016lx in %s]", moduleFrameList[i].m_Frames[frameIndex], symbolFilename.str().c_str());
            }
          }
        }

        char* lineBuffer = nullptr;
        size_t length;
        getline(&lineBuffer, &length, fd);
        
        char* filename = nullptr;
        int lineNumber;   
        int elementsFilled = sscanf(lineBuffer, "%m[^:]:%d %*s", &filename, &lineNumber);
        free(lineBuffer);
        if ( elementsFilled == 0 )
        {
          qDebug() << "Unexpected response from addr2line. Aborting.";
          pclose(fd);
          return false;
        }
        else if ( elementsFilled == 1 )
        {
          lineNumber = -1;
        }
  
        ResolvedSymbol out_sym;
        out_sym.m_Rip = moduleFrameList[i].m_Frames[frameIndex];
        out_sym.m_SymbolName = symbolBuffer;
        out_sym.m_FileName = filename;
        out_sym.m_LineNumber = lineNumber;
        out_sym.m_ModuleIndex = i;
        out_sym.m_Displacement = -1;
        resolvedSymbolsOut->push_back(out_sym);
        ++frameIndex;
        ++resolve_count;
        ++completed;
        free(filename);
        if (0 == (completed & 0x400))
        {
          reportProgress(completed, total);
        }
      }
      free(symbolBuffer);    
      pclose(fd);
    } while ( frameIndex < moduleFrameList[i].m_Frames.count() );
  }

  return true;
}
