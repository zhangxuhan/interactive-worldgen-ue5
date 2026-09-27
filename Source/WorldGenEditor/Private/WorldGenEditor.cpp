#include "WorldGenEditor.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldGenEditor, Log, All);

void FWorldGenEditorModule::StartupModule()
{
	UE_LOG(LogWorldGenEditor, Display, TEXT("WorldGenEditor module started (editor-only)"));
}

void FWorldGenEditorModule::ShutdownModule()
{
	UE_LOG(LogWorldGenEditor, Display, TEXT("WorldGenEditor module shut down"));
}

IMPLEMENT_MODULE(FWorldGenEditorModule, WorldGenEditor)
