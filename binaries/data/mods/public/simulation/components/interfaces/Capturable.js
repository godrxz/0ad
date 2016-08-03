Engine.RegisterInterface("Capturable");

/**
 * Message of the form { "capturePoints": number[] }
 * where "capturePoints" value is an array indexed by players id,
 * sent from Capturable component.
 */
Engine.RegisterMessageType("CapturePointsChanged");

/**
 * Message in the form of { "regenerating": boolean, "rate": number, "territoryDecay": number }
 * or in the form { "ticking": boolean, "rate": number, "territoryDecay": number }
 * where "rate" value is always zero when not decaying,
 * sent from Capturable component.
 */
Engine.RegisterMessageType("CaptureRegenStateChanged");

