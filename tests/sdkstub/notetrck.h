#pragma once
#include "max.h"
class NoteKey { public: TimeValue time; MSTR note; DWORD flags; NoteKey(TimeValue t, const MSTR& n, DWORD f = 0) : time(t), note(n), flags(f) {} };
class NoteKeyTab : public Tab<NoteKey*> {};
class NoteTrack : public ReferenceTarget {};
class DefNoteTrack : public NoteTrack { public: NoteKeyTab keys; };
inline NoteTrack* NewDefaultNoteTrack() { return nullptr; }
