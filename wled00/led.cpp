#include "wled.h"

/*
 * LED methods
 */

 // applies chosen setment properties to legacy values
void setValuesFromSegment(uint8_t s) {
  const Segment& seg = strip.getSegment(s);
  colPri[0] = R(seg.colors[0]);
  colPri[1] = G(seg.colors[0]);
  colPri[2] = B(seg.colors[0]);
  colPri[3] = W(seg.colors[0]);
  colSec[0] = R(seg.colors[1]);
  colSec[1] = G(seg.colors[1]);
  colSec[2] = B(seg.colors[1]);
  colSec[3] = W(seg.colors[1]);
  effectCurrent   = seg.mode;
  effectSpeed     = seg.speed;
  effectIntensity = seg.intensity;
  effectPalette   = seg.palette;
}


// applies global legacy values (colPri, colSec, effectCurrent...) to each selected segment
void applyValuesToSelectedSegs() {
  for (unsigned i = 0; i < strip.getSegmentsNum(); i++) {
    Segment& seg = strip.getSegment(i);
    if (!(seg.isActive() && seg.isSelected())) continue;
    if (effectSpeed     != seg.speed)     {seg.speed     = effectSpeed;     stateChanged = true;}
    if (effectIntensity != seg.intensity) {seg.intensity = effectIntensity; stateChanged = true;}
    if (effectPalette   != seg.palette)   {seg.setPalette(effectPalette);}
    if (effectCurrent   != seg.mode)      {seg.setMode(effectCurrent);}
    uint32_t col0 = RGBW32(colPri[0], colPri[1], colPri[2], colPri[3]);
    uint32_t col1 = RGBW32(colSec[0], colSec[1], colSec[2], colSec[3]);
    if (col0 != seg.colors[0])            {seg.setColor(0, col0);}
    if (col1 != seg.colors[1])            {seg.setColor(1, col1);}
  }
}


void toggleOnOff()
{
  if (bri == 0)
  {
    bri = briLast;
    strip.restartRuntime();
  } else
  {
    briLast = bri;
    bri = 0;
  }
  stateChanged = true;
}


//scales the brightness with the briMultiplier factor
byte scaledBri(byte in)
{
  unsigned val = ((unsigned)in*briMultiplier)/100;
  if (val > 255) val = 255;
  return (byte)val;
}


static byte briTFrac = 0; // fractional part of briT (1/256 code steps), rendered via spatial dithering during transitions

//applies global temporary brightness (briT) to strip
void applyBri() {
  if (realtimeOverride || !(realtimeMode && arlsForceMaxBri))
  {
    //DEBUG_PRINTF_P(PSTR("Applying strip brightness: %d (%d,%d)\n"), (int)briT, (int)bri, (int)briOld);
    strip.setBrightness16(((uint16_t)briT << 8) | briTFrac);
  }
}


//applies global brightness and sets it as the "current" brightness (no transition)
void applyFinalBri() {
  briOld = bri;
  briT = bri;
  briTFrac = 0;
  applyBri();
  strip.trigger(); // force one last update
}


//called after every state changes, schedules interface updates, handles brightness transition and nightlight activation
//unlike colorUpdated(), does NOT apply any colors or FX to segments
void stateUpdated(byte callMode) {
  //call for notifier -> 0: init 1: direct change 2: button 3: notification 4: nightlight 5: other (No notification)
  //                     6: fx changed 7: hue 8: preset cycle 9: blynk 10: alexa 11: ws send only 12: button preset
  setValuesFromFirstSelectedSeg();  // a much better approach would be to use main segment: setValuesFromMainSeg()

  if (bri != briOld || stateChanged) {
    if (stateChanged) currentPreset = 0; //something changed, so we are no longer in the preset

    if (callMode != CALL_MODE_NOTIFICATION && callMode != CALL_MODE_NO_NOTIFY) notify(callMode);
    if (bri != briOld && nodeBroadcastEnabled) sendSysInfoUDP(); // update on state

    //set flag to update ws and mqtt
    interfaceUpdateCallMode = callMode;
  } else {
    if (nightlightActive && !nightlightActiveOld && callMode != CALL_MODE_NOTIFICATION && callMode != CALL_MODE_NO_NOTIFY) {
      notify(CALL_MODE_NIGHTLIGHT);
      interfaceUpdateCallMode = CALL_MODE_NIGHTLIGHT;
    }
  }

  unsigned long now = millis();
  if (callMode != CALL_MODE_NO_NOTIFY && nightlightActive && (nightlightMode == NL_MODE_FADE || nightlightMode == NL_MODE_COLORFADE)) {
    briNlT = bri;
    nightlightDelayMs -= (now - nightlightStartTime);
    nightlightStartTime = now;
  }
  if (briT == 0) {
    if (callMode != CALL_MODE_NOTIFICATION) strip.resetTimebase(); //effect start from beginning
  }

  if (bri > 0) briLast = bri;

  //deactivate nightlight if target brightness is reached
  if (bri == nightlightTargetBri && callMode != CALL_MODE_NO_NOTIFY && nightlightMode != NL_MODE_SUN) nightlightActive = false;

  // notify usermods of state change
  UsermodManager::onStateChange(callMode);

  if (strip.getTransition() == 0) {
    jsonTransitionOnce = false;
    transitionActive = false;
    applyFinalBri();
    strip.trigger();
    // restore the configured default: a one-time "tt":0, an individual-LED ("i") command or a UDP-synced transition of 0
    // would otherwise leave the strip transition at 0 forever, making all subsequent fades instant
    strip.setTransition(transitionDelay);
  } else {
    if (transitionActive) {
      briOld = briT;
    } else if (bri != briOld || stateChanged)
      strip.setTransitionMode(true); // force all segments to transition mode
    transitionActive = true;
    transitionStartTime = now;
  }
  stateChanged = false;
}


void updateInterfaces(uint8_t callMode) {
  if (!interfaceUpdateCallMode || millis() - lastInterfaceUpdate < INTERFACE_UPDATE_COOLDOWN) return;

  sendDataWs();
  lastInterfaceUpdate = millis();
  interfaceUpdateCallMode = CALL_MODE_INIT; //disable further updates

  if (callMode == CALL_MODE_WS_SEND) return;

  #ifndef WLED_DISABLE_ALEXA
  if (espalexaDevice != nullptr && callMode != CALL_MODE_ALEXA) {
    espalexaDevice->setValue(bri);
    espalexaDevice->setColor(colPri[0], colPri[1], colPri[2]);
  }
  #endif
  #ifndef WLED_DISABLE_MQTT
  publishMqtt();
  #endif
}


void handleTransitions() {
  //handle still pending interface update
  updateInterfaces(interfaceUpdateCallMode);

  static unsigned long lastTransitionTick = 0;
  unsigned long nowMs = millis();
  unsigned long sinceTick = lastTransitionTick ? nowMs - lastTransitionTick : 0;
  lastTransitionTick = nowMs;
  if (transitionActive && strip.getTransition() > 0) {
    // if the main loop stalled (flash write, Wi-Fi hiccup, ...) shift the transition anchor so the fade
    // pauses for the stall instead of skipping ahead — a swallowed fade start reads as a jump to half brightness
    if (sinceTick > 50) transitionStartTime += sinceTick - 25;
    int ti = nowMs - transitionStartTime;
    if (ti < 0) ti = 0; // guard: a state change processed during a stall can leave the anchor in the future
    int tr = strip.getTransition();
    if (ti/tr) {
      strip.setTransitionMode(false); // stop all transitions
      // restore (global) transition time if not called from UDP notifier or single/temporary transition from JSON (also playlist)
      if (jsonTransitionOnce) strip.setTransition(transitionDelay);
      transitionActive = false;
      jsonTransitionOnce = false;
      applyFinalBri();
      return;
    }
    byte briTO = briT, briTFracO = briTFrac;
    if (gammaCorrectBri) {
      // brightness values are already perceptual (gamma is applied in strip.setBrightness()), interpolate linearly
      int deltaBri = (int)bri - (int)briOld;
      briT = briOld + (deltaBri * ti / tr);
      briTFrac = 0; // gamma LUT is 8 bit, no fractional brightness
      if (briT == 0 && (bri || briOld)) briT = 1; // hold minimum brightness until the transition ends (prevents early blackout on fade-out and late pop-in on fade-in)
    } else {
      // interpolate in a perceptual (approx. gamma 2.0) domain with 12-bit precision so equal time steps
      // give roughly equal perceived change, instead of a fade that jumps up quickly and then crawls
      // (or lingers bright and drops abruptly when fading out)
      uint32_t x = ((uint32_t)ti << 16) / tr; // transition progress, 0..65535
      // fade-in additionally eases out (immediate onset, gentle arrival): a purely perceptual ramp from
      // black spends its first third invisibly dark, which reads as a pause followed by a jump
      if (bri > briOld) x = 65535 - (((65535 - x) * (65535 - x)) >> 16);
      uint32_t p0 = sqrt32_bw((uint32_t)briOld * 255 * 256); // 0..4080, perceptual endpoints in 8.4 fixed point
      uint32_t p1 = sqrt32_bw((uint32_t)bri    * 255 * 256);
      uint32_t p  = p0 + (((int32_t)(p1 - p0) * (int32_t)x) >> 16);
      uint32_t b16 = (p * p + 127) / 255; // back to linear light in 8.8 fixed point (0xFF00 = full)
      if (b16 == 0 && (bri || briOld)) b16 = 1; // keep a trace of light until the transition ends (prevents early blackout on fade-out and late pop-in on fade-in)
      briT = b16 >> 8;
      briTFrac = b16 & 0xFF; // fraction is rendered by spatial dithering -> sub-code fade resolution at low brightness
    }
    if (briTO != briT || briTFracO != briTFrac) applyBri();
  }
}


// legacy method, applies values from col, effectCurrent, ... to selected segments
void colorUpdated(byte callMode) {
  applyValuesToSelectedSegs();
  stateUpdated(callMode);
}


void handleNightlight() {
  unsigned long now = millis();
  if (now < 100 && lastNlUpdate > 0) lastNlUpdate = 0; // take care of millis() rollover
  if (now - lastNlUpdate < 100) return; // allow only 10 NL updates per second
  lastNlUpdate = now;

  if (nightlightActive)
  {
    if (!nightlightActiveOld) //init
    {
      nightlightStartTime = millis();
      nightlightDelayMs = (unsigned)(nightlightDelayMins*60000);
      nightlightActiveOld = true;
      briNlT = bri;
      for (unsigned i=0; i<4; i++) colNlT[i] = colPri[i]; // remember starting color
      if (nightlightMode == NL_MODE_SUN)
      {
        //save current
        colNlT[0] = effectCurrent;
        colNlT[1] = effectSpeed;
        colNlT[2] = effectPalette;

        strip.getFirstSelectedSeg().setMode(FX_MODE_STATIC); // make sure seg runtime is reset if it was in sunrise mode
        effectCurrent = FX_MODE_SUNRISE;            // colorUpdated() will take care of assigning that to all selected segments
        effectSpeed = nightlightDelayMins;
        effectPalette = 0;
        if (effectSpeed > 60) effectSpeed = 60; //currently limited to 60 minutes
        if (bri) effectSpeed += 60; //sunset if currently on
        briNlT = !bri; //true == sunrise, false == sunset
        if (!bri) bri = briLast;
        colorUpdated(CALL_MODE_NO_NOTIFY);
      }
    }
    float nper = (millis() - nightlightStartTime)/((float)nightlightDelayMs);
    if (nightlightMode == NL_MODE_FADE || nightlightMode == NL_MODE_COLORFADE)
    {
      bri = briNlT + ((nightlightTargetBri - briNlT)*nper);
      if (nightlightMode == NL_MODE_COLORFADE)                                         // color fading only is enabled with "NF=2"
      {
        for (unsigned i=0; i<4; i++) colPri[i] = colNlT[i]+ ((colSec[i] - colNlT[i])*nper);   // fading from actual color to secondary color
      }
      colorUpdated(CALL_MODE_NO_NOTIFY);
    }
    if (nper >= 1) //nightlight duration over
    {
      nightlightActive = false;
      if (nightlightMode == NL_MODE_SET)
      {
        bri = nightlightTargetBri;
        colorUpdated(CALL_MODE_NO_NOTIFY);
      }
      if (bri == 0) briLast = briNlT;
      if (nightlightMode == NL_MODE_SUN)
      {
        if (!briNlT) { //turn off if sunset
          effectCurrent = colNlT[0];
          effectSpeed = colNlT[1];
          effectPalette = colNlT[2];
          toggleOnOff();
          applyFinalBri();
        }
      }

      if (macroNl > 0)
        applyPreset(macroNl);
      nightlightActiveOld = false;
    }
  } else if (nightlightActiveOld) //early de-init
  {
    if (nightlightMode == NL_MODE_SUN) { //restore previous effect
      effectCurrent = colNlT[0];
      effectSpeed = colNlT[1];
      effectPalette = colNlT[2];
      colorUpdated(CALL_MODE_NO_NOTIFY);
    }
    nightlightActiveOld = false;
  }
}

//utility for FastLED to use our custom timer
uint32_t get_millisecond_timer() {
  return strip.now;
}
