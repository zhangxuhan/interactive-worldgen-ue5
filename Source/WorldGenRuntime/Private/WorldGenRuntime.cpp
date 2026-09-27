#include "WorldGenRuntime.h"

DEFINE_LOG_CATEGORY(LogWorldGenRT);

void FWorldGenRuntimeModule::StartupModule()
{
	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenRuntime module started"));
}

void FWorldGenRuntimeModule::ShutdownModule()
{
	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenRuntime module shut down"));
}

IMPLEMENT_MODULE(FWorldGenRuntimeModule, WorldGenRuntime)
