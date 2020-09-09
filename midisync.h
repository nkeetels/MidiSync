
// tpbds -- midisync system

#ifndef MIDISYNC_H
#define MIDISYNC_H

#include <map>
#include <vector>
#include <string.h>

namespace KDLib {

class MIDIEvent;
class MIDITrack;
class MIDISong
{
public:
  MIDISong();
  virtual ~MIDISong();

public:
  bool Load(uint8_t *songData);
  void Destroy();
  void Play();
  void Stop();
  void Reset();
  void Update(double deltatime);
  void SetTempo(uint32_t tempo);
  uint32_t GetDivision() const;
  uint32_t GetTempo() const;
  bool IsNoteOn(uint32_t channel, uint8_t &note, uint8_t &velocity);
  bool GetChannelIndex(const char *tag, uint32_t &index) const;
  bool IsPlaying() const;

private:
  std::map<uint32_t, MIDIEvent*> m_currentEvents;
  std::vector<MIDITrack*>m_tracks;
  uint32_t m_tempo;
  uint32_t m_division;
  double m_playTime;
  bool m_isPlaying;
};

MIDISong *LoadSong(uint8_t *songData);

} // END OF NAMESPACE KDLIB

#endif//__KDMUSIC_H
