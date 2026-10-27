target_sources(${PROJECT_NAME}
        PRIVATE
        AbyssConfiguration.c AbyssConfiguration.h
        CrashHandler.c CrashHandler.h
        FileManager.c FileManager.h
        Mods.c Mods.h
        Terminal.c Terminal.h
        Globals.c Globals.h
        Logging.c Logging.h
        MemoryStream.c MemoryStream.h
        MpqStream.c MpqStream.h
        ResourcePaths.h
        Revision.c Revision.h revision_data.h.in
        RingBuffer.c RingBuffer.h
)