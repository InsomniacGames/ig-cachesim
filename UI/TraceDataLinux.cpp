#include "Precompiled.h"
#include "TraceData.h"
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
CacheSim::TraceData::ResolveResult CacheSim::TraceData::symbolResolveTask()
{
  
  // Pick out input data.
  const SerializedHeader* hdr = reinterpret_cast<const SerializedHeader*>(m_Data);

  const SerializedModuleEntry* modules = hdr->GetModules();
  const uint32_t moduleCount = hdr->GetModuleCount();
  const uintptr_t* stacks  = hdr->GetStacks();
  const uint32_t stackCount = hdr->GetStackCount();
  const SerializedNode* nodes   = hdr->GetStats();
  const uint32_t nodeCount = hdr->GetStatCount();

  struct ModuleFrames
  {
      const SerializedModuleEntry* m_Entry;
      QVector<uintptr_t> m_Frames;
  };

  QVector<ModuleFrames> moduleFrameList;
  for ( int i = 0; i < moduleCount; i++ )
  {
      ModuleFrames frames;
      frames.m_Entry = &modules[i];
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

    for ( int i = 0; i < moduleCount; i++ )
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

  
  int total = 2 * (stackCount + nodeCount); // Two passes, once to sort the data and once to process it
  int completed = 0;

  // Sort instructions in stacks
  for (uint32_t i = 0; i < stackCount; ++i, ++completed)
  {
    if (uintptr_t rip = stacks[i])
    {
      addRipToModuleFrames(rip);
    }

    if (0 == (completed & 0x400))
    {
      Q_EMIT symbolResolutionProgressed(completed, total);
    }
  }

  // Sort any instructions used in leaf functions.
  for (uint32_t i = 0; i < nodeCount; ++i, ++completed)
  {
    addRipToModuleFrames(nodes[i].m_Rip);

    if (0 == (completed & 0x400))
    {
      Q_EMIT symbolResolutionProgressed(completed, total);
    }
  }

  Q_EMIT symbolResolutionProgressed(completed, total);

  QVector<SerializedSymbol> symbols;

  QVector<QChar> stringData;
  stringData.push_back(QChar(0));    // Zero offset strings point here.

  QHash<QString, uint32_t> stringLookup;

  auto intern_qstring = [&](const QString& s) -> uint32_t
  {
    auto it = stringLookup.find(s);
    if (it != stringLookup.end())
    {
      return it.value();
    }
    uint32_t result = stringData.size();
    Q_FOREACH (QChar ch, s)
    {
      stringData.append(ch);
    }
    stringData.append(QChar(0));
    return result;
  };

  auto intern_string = [&](const char* str) -> uint32_t
  {
    return intern_qstring(QString::fromUtf8(str));
  };

  for ( int i = 0; i < moduleFrameList.count(); i++ )
  {
    if ( moduleFrameList[i].m_Frames.count() == 0 )
    {
        continue;
    }

    // Figure out the actual file with the debug info. If there's anything in /usr/lib/debug, use that version instead
    const char* baseFilename = hdr->GetModuleName(*moduleFrameList[i].m_Entry);
    // resolve any symlinks
    char resolvedBaseFilename[1024];
    if ( realpath(baseFilename, resolvedBaseFilename) == nullptr )
    {
      strcpy(resolvedBaseFilename, baseFilename);
    }
    if (strlen(baseFilename)  == 0)
    { 
      printf("Pausing\n");
      getchar();
    }
    const char* debugLibBase = "/usr/lib/debug";
    std::stringstream symbolFilename;
    symbolFilename << debugLibBase;
    symbolFilename << resolvedBaseFilename;
    if ( access(symbolFilename.str().c_str(), R_OK) == -1 )
    {
      // Didn't find a special symbol library, so use the code library
      symbolFilename.str(baseFilename);
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
        ResolveResult r;
        return r;
      }

      char* symbolBuffer = nullptr;
      size_t symbolBufferSize;
      while ( getline(&symbolBuffer, &symbolBufferSize, fd) != -1 )
      {
        if ( strcmp(symbolBuffer, "??\n") == 0 )
        {
          // Attempt to use objdump to get the data
          if ( !ResolveSymbolViaObjdump(&symbolBuffer, &symbolBufferSize, hdr->GetModuleName(*moduleFrameList[i].m_Entry), moduleFrameList[i].m_Frames[frameIndex] - moduleFrameList[i].m_Entry->m_ImageBase))
          {
            int needed = snprintf(symbolBuffer, symbolBufferSize, "[%016lx in %s]", moduleFrameList[i].m_Frames[frameIndex], symbolFilename.str().c_str());
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
          ResolveResult r;
          pclose(fd);
          return r;
        }
        else if ( elementsFilled == 1 )
        {
          lineNumber = -1;
        }
  
        SerializedSymbol out_sym = { moduleFrameList[i].m_Frames[frameIndex] };
        out_sym.m_SymbolName = intern_string(symbolBuffer);
        out_sym.m_FileName = intern_string(filename);
        out_sym.m_LineNumber = lineNumber;
        out_sym.m_ModuleIndex = i;
        out_sym.m_Displacement = -1;
        symbols.push_back(out_sym);
        ++frameIndex;
        ++resolve_count;
        ++completed;
        free(filename);
        if (0 == (completed & 0x400))
        {
          Q_EMIT symbolResolutionProgressed(completed, total);
        }
      }
      free(symbolBuffer);    
      pclose(fd);
    } while ( frameIndex < moduleFrameList[i].m_Frames.count() );
  }

  std::sort(symbols.begin(), symbols.end(), [](const SerializedSymbol& l, const SerializedSymbol& r) -> bool
  {
    return l.m_Rip < r.m_Rip;
  });

  Q_EMIT symbolResolutionProgressed(completed, total);

  ResolveResult result;
  result.m_StringData = stringData;
  result.m_Symbols = symbols;
  return result;
}
