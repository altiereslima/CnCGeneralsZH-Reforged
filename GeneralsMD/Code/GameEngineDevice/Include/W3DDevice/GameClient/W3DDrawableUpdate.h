/* Per-view cadence for composing drawable render poses. */
#pragma once

#ifndef __W3DDRAWABLEUPDATE_H_
#define __W3DDRAWABLEUPDATE_H_

class W3DDrawableUpdate
{
public:
	W3DDrawableUpdate() : m_haveFrame(false), m_lastFrame(0) {}

	bool needsUpdate(unsigned clientFrame, bool animationAdvanced, unsigned videoPicture)
	{
		// Extra capture pictures use the first picture's pose and must not consume a new frame.
		if (videoPicture != 0)
			return false;
		// A flushed client frame can replace a render pose with its raw transform even when
		// pause has stopped W3D's animation clock. Compose that frame once for each view.
		if (!animationAdvanced && m_haveFrame && clientFrame == m_lastFrame)
			return false;
		m_haveFrame = true;
		m_lastFrame = clientFrame;
		return true;
	}

	void reset()
	{
		m_haveFrame = false;
		m_lastFrame = 0;
	}

private:
	bool m_haveFrame;
	unsigned m_lastFrame;
};

#endif // __W3DDRAWABLEUPDATE_H_
