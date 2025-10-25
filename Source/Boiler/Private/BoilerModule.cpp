#include "BoilerModule.h"

THIRD_PARTY_INCLUDES_START
#include <openvdb/openvdb.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "Boiler"

void FBoiler::StartupModule() {

}

void FBoiler::ShutdownModule() {

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBoiler, Boiler);
