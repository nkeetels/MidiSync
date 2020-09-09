
#include <map>
#include <algorithm>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "midisync.h"

using namespace std;

namespace KDLib {

  // Convert a string to lowercase
string ToLower(string s)
{
  transform(s.begin(), s.end(), s.begin(), ::tolower);
  return s;
}

#pragma pack(push, 1)

struct MIDIHeader
{
  uint32_t ID;
  uint32_t chunkSize;
  uint16_t format;
  uint16_t numTracks;
  uint16_t division;
};

struct MIDITrackHeader
{
  uint32_t ID;
  uint32_t length;
};

#pragma pack(pop)

// swap byte-order to Little Endian
void SwapShort(uint16_t *v)
{
  uint8_t a = *v & 0xff;
  uint8_t b = (*v >> 8) & 0xff;
  *v = (a << 8) | b;
}

// swap byte-order to Little Endian
void SwapLong(uint32_t *v)
{
  uint8_t a = *v & 0xff;
  uint8_t b = (*v >> 8) & 0xff;
  uint8_t c = (*v >> 16) & 0xff;
  uint8_t d = (*v >> 24) & 0xff;
  *v = (a << 24) | (b << 16) | (c << 8) | d;
}

// Get variable length buffer, returns length
uint32_t GetVLQ(uint8_t **data)
{
  uint32_t length = 0;
  uint8_t c = 0;
  do
  {
    c = **data;
    *data = (*data)+1;
    length = (length << 7) + (c & 0x7f);
  } while (c & 0x80);

  return length;
}

class MIDIEvent
{
public:
  MIDIEvent(double time, uint8_t status, uint8_t channel, uint8_t param1, uint8_t param2, uint8_t *data, uint32_t length);
  virtual ~MIDIEvent();

public:
  void SetTime(double time);
  void SetDuration(double duration);
  double GetTime() const;
  double GetDuration() const;
  uint8_t GetStatus() const;
  uint8_t GetChannel() const;
  uint8_t GetParam1() const;
  uint8_t GetParam2() const;
  uint8_t *GetData() const;
  uint32_t GetLength() const;
  void Destroy();

public:
  bool m_processed;

private:
  double m_time;
  double m_duration;
  uint8_t m_status;
  uint8_t m_channel;
  uint8_t m_param1;
  uint8_t m_param2;
  uint8_t *m_data;
  uint32_t m_length;
};

MIDIEvent::MIDIEvent(double time, uint8_t status, uint8_t channel, uint8_t param1, uint8_t param2, uint8_t *data, uint32_t length)
  : m_time(time), m_status(status), m_channel(channel), m_param1(param1), m_param2(param2), m_data(data), m_length(length), m_duration(0), m_processed(false)
{}

MIDIEvent::~MIDIEvent()
{}

// Sets the time (in seconds) at which the event should be fired
void MIDIEvent::SetTime(double time)
{
  m_time = time;
}

// Sets the duration of the MIDI event in seconds
void MIDIEvent::SetDuration(double duration)
{
  m_duration = duration;
}

// Returns the time (in seconds) at which the event should be fired
double MIDIEvent::GetTime() const
{
  return m_time;
}

// Returns the duration of the MIDI event in seconds
double MIDIEvent::GetDuration() const
{
  return m_duration;
}

// Returns the MIDI status-byte of the event
uint8_t MIDIEvent::GetStatus() const
{
  return m_status;
}

// Returns the MIDI channel that the event operates on (not to be confused with MIDI track)
uint8_t MIDIEvent::GetChannel() const
{
  return m_channel;
}

// Returns the first parameter of single- and multi-channel MIDI events (often denotes note-number)
uint8_t MIDIEvent::GetParam1() const
{
  return m_param1;
}

// Returns the second parameter of single- and multi-channel MIDI events (often denotes velocity)
uint8_t MIDIEvent::GetParam2() const
{
  return m_param2;
}

// Returns the variable length data buffer of the event
uint8_t *MIDIEvent::GetData() const
{
  return m_data;
}

// Returns the lenth of the variable length data buffer of the event
uint32_t MIDIEvent::GetLength() const
{
  return m_length;
}

// Destroy midi event
void MIDIEvent::Destroy()
{
  m_data = 0;
}

class MIDISong;
class MIDITrack
{
  friend class MIDISong;
public:
  MIDITrack(MIDISong *owner);
  virtual ~MIDITrack();

public:
  bool Parse(uint8_t **songData);
  void Destroy();
  string GetTrackName() const;

private:
  MIDISong *m_midiSong; 
  uint8_t *m_eventBuffer;
  uint8_t *m_event;
  uint8_t *m_nextEvent;
  char m_trackName[256];
  char m_instrumentName[256];
  map<uint32_t, MIDIEvent*> m_map;
};

MIDITrack::MIDITrack(MIDISong *owner)
: m_eventBuffer(0), m_event(0), m_nextEvent(0), m_midiSong(owner)
{
}

MIDITrack::~MIDITrack()
{
}

// Destroy MIDI track
void MIDITrack::Destroy()
{
  for (map<uint32_t, MIDIEvent*>::iterator iter = m_map.begin(); iter != m_map.end(); ++iter)
  {
    (*iter).second->Destroy();
    delete (*iter).second;
  }

  m_map.clear();

  delete[] m_eventBuffer;
  m_event = 0;
  m_midiSong = 0;
}

// Parses a single MIDI track
bool MIDITrack::Parse(uint8_t **songData)
{
  MIDITrackHeader *header = (MIDITrackHeader*)*songData;
  SwapLong(&header->length);

  if (header->ID != *(uint32_t*)"MTrk")
    return false;

  uint8_t *buffer = *songData + sizeof(MIDITrackHeader);
  m_eventBuffer = (uint8_t*)malloc(header->length);
  memcpy(m_eventBuffer, buffer, header->length);

  *songData = buffer + header->length;
  m_event = m_eventBuffer;

  uint8_t status = 0;
  uint8_t currentStatus = 0;
  uint8_t channel = 0;
  uint8_t param1 = 0;
  uint8_t param2 = 0;
  uint8_t *data = 0;
  uint32_t length = 0;
  uint32_t runtime = 0;
  uint32_t deltatime = 0;

  while (static_cast<uint32_t>(m_event - m_eventBuffer) < header->length)
  {
    // Read the delta time at which the event should fire
    deltatime = GetVLQ(&m_event);
    runtime += deltatime;

    // Convert to time in seconds
    double tick = (((double)runtime / (m_midiSong->GetDivision()) * m_midiSong->GetTempo())) / 1000000.0;
    tick *= 65536.0;

    // If there's no new event then fall back to the previous status
    if (*m_event < 0x80)
    {
      m_event--;
    }
    else
    {
      status = *m_event >> 4;
      channel = *m_event & 0xf;
    }

    // Decode events
    switch (status)
    {
    case 0xC:
    case 0xD: // Single-channel MIDI event (such as channel pressure, program change, etc)
      param1 = *(m_event + 1);
      m_nextEvent = m_event + 2;
      break;
    case 0xF:
      if (channel == 0xF) // META event
      {
        param1 = *(m_event + 1);
        uint8_t *p2 = m_event + 2;
        length = GetVLQ(&p2);
        data = m_event + 3;
        m_nextEvent = m_event + 3 + length;
      }
      else // SYSEX event
      {
        uint8_t *p2 = m_event + 1;	
        length = GetVLQ(&p2);
        data = m_event + 2;
        m_nextEvent = m_event + 2 + length;
      }
      break;
    default: // Dual-channel MIDI event (such as note events)
      param1 = *(m_event + 1);
      param2 = *(m_event + 2);
      m_nextEvent = m_event + 3;
      break;
    }

    // Handle events
    switch (status)
    {
    default:
      {
        MIDIEvent *e = new MIDIEvent(runtime, status, channel, param1, param2, 0, 0);
        m_map[static_cast<uint32_t>(tick)] = e;
      }
      break;
    case 0x8: // note off
    case 0x9: // note on
      {
        MIDIEvent *e = new MIDIEvent(runtime, status, channel, param1, param2, 0, 0);
        m_map[static_cast<uint32_t>(tick)] = e;
      }
      break;
    case 0xF: // sysex
      switch (channel)
      {
        case 0x0:
        case 0x7:
          {
            MIDIEvent *e = new MIDIEvent(runtime, status * 16 + channel, 0, 0, 0, data, length);
            m_map[static_cast<uint32_t>(tick)] = e;
          }
          break;
        case 0xF: // meta event
          switch (param1)
          {
            case 0x03: // track name
              strcpy_s(m_trackName, 256, (char*)data);
              break;
            case 0x04: // instrument name
              strcpy_s(m_instrumentName, 256, (char*)data);
              break;
            case 0x2F: // end of track
              break;
            case 0x51: // change tempo
              {
                uint32_t tempo = (((*data)<<16) + ((*(data+1))<<8) + *(data+2));
                m_midiSong->SetTempo(tempo);
              }
              break;
          }
          break;
      }
      break;
    }
  	m_event = m_nextEvent;
  }

  return true;
}

// Returns the name of the track (as specified in the DAW)
string MIDITrack::GetTrackName() const
{
  return m_trackName;
}

MIDISong::MIDISong()
  : m_tempo(0), m_playTime(0), m_division(0)
{
}

MIDISong::~MIDISong()
{
}

// Parses a MIDI file buffer
bool MIDISong::Load(uint8_t *songData)
{
  // Parse MIDI file header
  MIDIHeader *header = (MIDIHeader*)songData;
  SwapShort(&header->format);
  SwapShort(&header->numTracks);
  SwapShort(&header->division);

  m_division = header->division;

  // This parser only supports single-track and multi-track MIDI files, not the extended General MIDI 2 spec
  if (header->ID != *(uint32_t*)"MThd" || header->format >= 2)
    return false;

  songData += sizeof(MIDIHeader);

  // Parse seperate tracks
  for (int i = 0; i < header->numTracks; i++)
  {
    MIDITrack *track = new MIDITrack(this);
    track->Parse(&songData);

    m_tracks.push_back(track);
  }
  return true;
}

// Start playback
void MIDISong::Play()
{
  m_playTime = 0.0;
  m_isPlaying = true;
}

// Stop playback
void MIDISong::Stop()
{
  m_isPlaying = false;
}

// Reset playcursor
void MIDISong::Reset()
{
  m_playTime = 0.0;
}

// Song player code
void MIDISong::Update(double deltatime)
{
  if (!m_isPlaying)
    return;

  // Clear the eventbuffer that contains all note events across all tracks for the current tick
  m_currentEvents.clear();

  // Advance play cursor
  m_playTime += deltatime;

  uint32_t tick = static_cast<uint32_t>(m_playTime * 65536);

  for (uint32_t i = 0; i < m_tracks.size(); i++)
  {
    MIDITrack *track = m_tracks[i];

    // For a given track, fire all unplayed noteevents up untill the current playtime
    std::map<uint32_t, MIDIEvent*>::iterator iter = track->m_map.begin();
    while (iter != track->m_map.end() && (*iter).first <= tick)
  	{
      MIDIEvent *e = (*iter).second;

      // Only handle the events that interest us 
      if (!e->m_processed && e->GetStatus() != 0x8 && e->GetChannel() != 0xF)
      {
        // Compile MIDI message
        uint8_t status = e->GetStatus();
        uint8_t channel = e->GetChannel();
        uint8_t param1 = e->GetParam1();
        uint8_t param2 = e->GetParam2();
        uint32_t message = ((param2)<<16) | ((param1)<<8) | (status * 16 + channel);

		// Mark as handled
        e->m_processed = true;

        // Notify NoteOn events
        if (e->GetStatus() == 0x9)
          m_currentEvents[i] = e;
      }

      ++iter;
  	}
  }
}

// Set playback tempo in microseconds per quarter note
void MIDISong::SetTempo(uint32_t tempo)
{
  m_tempo = tempo;
}

// Returns the number of divisions that the MIDI time-stamps are based on
uint32_t MIDISong::GetDivision() const
{
  return m_division;
}

// Returns the playback tempo in microseconds per quarter note
uint32_t MIDISong::GetTempo() const
{
  return m_tempo;
}

// Returns whether a NoteOn event has occurred on a given channel, and the note MIDI-code / velocity (should be called AFTER update)
bool MIDISong::IsNoteOn(uint32_t channel, uint8_t &note, uint8_t &velocity)
{
  if (channel >= m_tracks.size())
    return false;

  for (map<uint32_t, MIDIEvent*>::iterator iter = m_currentEvents.begin(); iter != m_currentEvents.end(); ++iter)
  {
    if ((*iter).first == channel)
    {
      note = (*iter).second->GetParam1();
      velocity = (*iter).second->GetParam2();
      return true;
    }      
  }
  return false;
}

// Returns true if the song is currently playing
bool MIDISong::IsPlaying() const
{
  return m_isPlaying;
}

// Returns the number of the channel specified by the string 'tag'
bool MIDISong::GetChannelIndex(const char *tag, uint32_t &index) const
{
  uint32_t i = 0;
  for (vector<MIDITrack*>::const_iterator iter = m_tracks.begin(); iter != m_tracks.end(); ++iter)
  {
    string s1 = ToLower(tag);
    string s2 = ToLower((*iter)->GetTrackName());

    if (s1.compare(s2) != string::npos)
    {
      index = i;
      return true;
    }
    ++i;
  }
  return false;
}

void MIDISong::Destroy()
{
  m_currentEvents.clear();

  for (vector<MIDITrack*>::iterator iter = m_tracks.begin(); iter != m_tracks.end(); ++iter)
  {
    (*iter)->Destroy();
    delete (*iter);
  }
  m_tracks.clear();
}

// Loads a MIDI file and returns a song
MIDISong *LoadSong(uint8_t *songData)
{
  MIDISong *song = new MIDISong();

  bool success = song->Load(songData);
  return success ? song : 0;
}

} // END OF NAMESPACE KDLIB