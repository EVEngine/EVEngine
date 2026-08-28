#include "editor/EditorAudioTransport.h"
#include "audio/Source.h"

namespace eve::editor {
void AudioSourceTransportBackend::play(){if(source_)source_->play();}
void AudioSourceTransportBackend::pause(){if(source_)source_->pause();}
void AudioSourceTransportBackend::stop(){if(source_)source_->stop();}
bool AudioSourceTransportBackend::seek(double seconds){return source_&&source_->seek(seconds);}
double AudioSourceTransportBackend::tell()const{return source_?source_->tell():0.0;}
double AudioSourceTransportBackend::duration()const{return source_?source_->getDuration():0.0;}
bool AudioSourceTransportBackend::playing()const{return source_&&source_->isPlaying();}
void AudioSourceTransportBackend::setNativeLooping(bool enabled){if(source_)source_->setLooping(enabled);}
}  // namespace eve::editor
