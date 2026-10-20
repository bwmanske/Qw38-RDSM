#pragma once

#include <windows.h>
#include <unknwn.h> // IUnknown, used by GDI+ COM interface declarations

// This SDK only defines PROPID in infotech.h, which drags in heavy COM headers
// (comdef.h, ocidl.h). GDI+ headers reference PROPID, so define it here to keep
// the GDI+ include lightweight. Redefining a typedef to the same type is allowed
// in C++11, so this is safe even if another header defines PROPID as well.
typedef DWORD PROPID;

// MIDL_INTERFACE is not provided by the usual COM headers in this SDK (it only
// appears in unrelated headers such as MqOaI.h). GDI+ headers (e.g.
// GdiplusImaging.h) use it to declare COM interfaces like IImageBytes.
#ifndef MIDL_INTERFACE
#define MIDL_INTERFACE(x) interface __declspec(novtable, uuid(x))
#endif

// Gdiplus::byte is missing from this SDK's gdiplusbase.h, but GDI+ headers
// (e.g. GdiplusPath.h) use it. Declare it in the Gdiplus namespace before the
// GDI+ headers open that namespace.
namespace Gdiplus {
typedef unsigned char byte;
}

#include <gdiplus.h>
