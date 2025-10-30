#pragma once
#include "pch.h"
#include "NES/NesConsole.h"
#include "NES/APU/NesApu.h"
#include "NES/APU/BaseExpansionAudio.h"
#include "Utilities/Serializer.h"

class Sunsoft5bAudio : public BaseExpansionAudio
{
private:
	// YM2149F tone (LUT) / noise / envelope emulation core from 0CC-FamiTracker
	uint8_t _volumeLut[32] = {};/* this table fucking clips and i don't understand why
	  0,   1,   1,   2,
	  2,   3,   3,   4,
	  5,   6,   7,   9,
	 11,  13,  15,  18,
	 22,  26,  31,  37,
	 45,  53,  63,  76,
	 90, 106, 127, 151,
	180, 212, 255, 255, 
	};
	*/
	uint8_t _currentRegister = 0;
	uint8_t _registers[0x10] = {};
	int16_t _lastOutput = 0;
	int16_t _timer[3] = {};
	uint8_t _toneStep[3] = {};
	bool _processTick = false;

	uint32_t m_iNoiseClock = 0;
	uint32_t m_iNoiseState = 65535;

	uint32_t m_iEnvelopeClock = 0;
	unsigned char m_iEnvelopeLevel = 0;
	unsigned char m_iEnvelopeShape = 0;
	bool m_bEnvelopeHold = true;

	unsigned char prevRegD = 0;


	uint16_t GetPeriod(int channel)
	{
		return _registers[channel * 2] | (_registers[channel * 2 + 1] << 8);
	}

	uint32_t GetEnvelopePeriod()
	{
		return _registers[0x0B] | (_registers[0x0C] << 8);
	}

	uint8_t GetNoisePeriod()
	{
		return _registers[6] ? _registers[6] & 0x1F : 1; // noise period 0 is the same as 1
		//return _registers[6] ? ((_registers[6] & 0x1F) << 5) : 0x10;
	}

	uint8_t GetConstantVolume(int channel)
	{
		return _registers[8 + channel] & 0x0F;
	}

	bool IsToneEnabled(int channel)
	{
		return ((_registers[7] >> channel) & 0x01) == 0x00;
	}

	bool IsNoiseEnabled(int channel)
	{
		return ((_registers[7] >> (channel + 3)) & 0x01) == 0x00;
	}

	bool IsEnvelopeEnabled(int channel)
	{
		return _registers[8 + channel] >> 4;
	}
	
	void UpdateChannel(int ch)
	{
		_timer[ch]--;
		if(_timer[ch] <= 0) {
			_timer[ch] = GetPeriod(ch);
			_toneStep[ch] = (_toneStep[ch] + 1) & 0x0F;
		}
	}

	void UpdateOutputLevel()
	{
		int16_t summedOutput = 0;
		bool Out = false;
		for(int i = 0; i < 3; i++) {
			if(IsToneEnabled(i) && IsNoiseEnabled(i)) {
				Out = (_toneStep[i] < 0x08) & (m_iNoiseState & 1);
			} else if(IsToneEnabled(i)) {
				Out = (_toneStep[i] < 0x08);
			} else if(IsNoiseEnabled(i)) {
				Out = (m_iNoiseState & 1);
			} else {
				Out = false;
			}
			summedOutput +=
				(IsEnvelopeEnabled(i) ? _volumeLut[m_iEnvelopeLevel] : _volumeLut[GetConstantVolume(i) << 1]) * !Out;
		}

		_console->GetApu()->AddExpansionAudioDelta(AudioChannel::Sunsoft5B, summedOutput - _lastOutput);
		_lastOutput = summedOutput;
	}

protected:
	void Serialize(Serializer& s) override
	{
		BaseExpansionAudio::Serialize(s);

		SVArray(_timer, 3);
		SVArray(_registers, 0x10);
		SVArray(_toneStep, 3);
		SV(_currentRegister); SV(_lastOutput); SV(_processTick);
	}

	void ClockAudio() override
	{
		if(_processTick) {
			for(int i = 0; i < 3; i++) {
				UpdateChannel(i);
			}

			if(prevRegD != _registers[0x0D]) {
				m_iEnvelopeClock = 0;
				m_iEnvelopeShape = _registers[0x0D];
				m_bEnvelopeHold = false;
				m_iEnvelopeLevel = (_registers[0x0D] & 0x04) ? 0 : 0x1F;
			}
			prevRegD = _registers[0x0D];

			m_iNoiseClock++;
			if(m_iNoiseClock >= GetNoisePeriod() << 4) {
				m_iNoiseClock = 0;
				if(m_iNoiseState & 0x01)
					m_iNoiseState ^= 0x24000;
				m_iNoiseState >>= 1;
			}
			
			/* ayumi's implementation
			m_iNoiseClock++;
			if(m_iNoiseClock >= (GetNoisePeriod() << 4)) { // i don't just understand why.
				m_iNoiseClock = 0;
				m_iNoiseState = (m_iNoiseState >> 1) | (((m_iNoiseState ^ (m_iNoiseState >> 3)) & 1) << 16);
			}
			*/

			m_iEnvelopeClock++;
			if(m_iEnvelopeClock >= (GetEnvelopePeriod() << 3) && (GetEnvelopePeriod() << 3)) {
				m_iEnvelopeClock = 0;
				if(!m_bEnvelopeHold) {
					m_iEnvelopeLevel += (m_iEnvelopeShape & 0x04) ? 1 : -1;
					m_iEnvelopeLevel &= 0x3F;
				}
				if(m_iEnvelopeLevel & 0x20) {
					if(m_iEnvelopeShape & 0x08) {
						if((m_iEnvelopeShape & 0x03) == 0x01 || (m_iEnvelopeShape & 0x03) == 0x02)
							m_iEnvelopeShape ^= 0x04;
						if(m_iEnvelopeShape & 0x01)
							m_bEnvelopeHold = true;
						m_iEnvelopeLevel = (m_iEnvelopeShape & 0x04) ? 0 : 0x1F;
					} else {
						m_bEnvelopeHold = true;
						m_iEnvelopeLevel = 0;
					}
				}
			}
			UpdateOutputLevel();
		}
		_processTick = !_processTick;
	}

public:
	Sunsoft5bAudio(NesConsole* console) : BaseExpansionAudio(console)
	{
		memset(_timer, 0, sizeof(_timer));
		memset(_registers, 0, sizeof(_registers));
		memset(_toneStep, 0, sizeof(_toneStep));
		_currentRegister = 0;
		_lastOutput = 0;
		_processTick = false;

		double output = 1.0;
		_volumeLut[0] = 0;
		for(int i = 1; i < 0x20; i++) {
			//+1.5 dB 2x for every 1 step in volume
			output *= 1.1885022274370184377301224648922;
			// output *= 1.1885022274370184377301224648922;

			_volumeLut[i] = (uint8_t)output;
		}
	}

	void WriteRegister(uint16_t addr, uint8_t value)
	{
		switch(addr & 0xE000) {
			case 0xC000:
				_currentRegister = value;
				break;

			case 0xE000:
				if(_currentRegister <= 0x0F) {
					_registers[_currentRegister] = value;

					/*if(value == 13) {
						m_iEnvelopeClock = 0;
						m_iEnvelopeShape = value;
						m_bEnvelopeHold = false;
						m_iEnvelopeLevel = (value & 0x04) ? 0 : 0x1F;
					}
					*/
				}
				break;
		}
	}
};