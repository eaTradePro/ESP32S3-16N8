#include "Pin.h"

// Pins:
#include "Pins/PinOptionsParser.h"
#include "Pins/VoidPinDetail.h"
#include "Pins/GPIOPinDetail.h"
#include "Pins/I2SPinDetail.h"

#if defined PIN_DEBUG && defined ESP32
#    include "Pins/DebugPinDetail.h"
#    include "Grbl.h"  // grbl_sendf
#endif

bool Pin::parse(String str, Pins::PinDetail*& pinImplementation) {
    // Initialize pinImplementation first! Callers might want to delete it, and we don't want a random pointer.
    pinImplementation = nullptr;

    if (str == "") {
        // Re-use undefined pins happens in 'create':
        pinImplementation = new Pins::VoidPinDetail();
        return true;
    }

    // Parse the definition: [GPIO].[pinNumber]:[attributes]
    auto nameStart = str.begin();
    auto idx       = nameStart;
    for (; idx != str.end() && *idx != '.' && *idx != ':'; ++idx) {
        *idx = char(::tolower(*idx));
    }
    String prefix = str.substring(0, int(idx - str.begin()));

    if (idx != str.end()) {  // skip '.'
        ++idx;
    }

    int pinNumber = 0;
    if (prefix != "") {
        if (idx == str.end()) {
            // Incorrect pin definition.
            return false;
        }

        for (; idx != str.end() && *idx >= '0' && *idx <= '9'; ++idx) {
            pinNumber = pinNumber * 10 + int(*idx - '0');
        }
        if (pinNumber < 0 || pinNumber > 253) {
            // Pin number has to be between [0,253].
            return false;
        }
    }

    String options;
    if (idx != str.end()) {
        if (*idx != ':') {
            // Pin definition attributes or EOF expected.
            return false;
        }
        ++idx;

        options = str.substring(int(idx - str.begin()));
    }

    // What would be a simple, practical way to parse the options? I figured, why not
    // just use the C-style string, convert it to lower case, and change the separators
    // into 'nul' tokens. We then pass the number of 'nul' tokens, and the first char*
    // which is pretty easy to parse.

    // Build an options parser:
    Pins::PinOptionsParser parser(options.begin(), options.end());

    // Build this pin:
    if (prefix == "gpio") {
        pinImplementation = new Pins::GPIOPinDetail(uint8_t(pinNumber), parser);
    } else if (prefix == "i2s") {
#ifdef ESP_32
        pinImplementation = new Pins::I2SPinDetail(uint8_t(pinNumber), parser);
#else
        return false;  // not supported
#endif
    } else if (prefix == "void") {
        // Note: having multiple void pins has its uses for debugging. Note that using
        // when doing 'x == Pin::UNDEFINED' will evaluate to 'false' if the pin number
        // is not 0.
        pinImplementation = new Pins::VoidPinDetail(uint8_t(pinNumber));
    }

#if defined PIN_DEBUG && defined ESP32
    pinImplementation = new Pins::DebugPinDetail(pinImplementation);
#endif
    return true;
}

Pin Pin::create(const String& str) {
    Pins::PinDetail* pinImplementation = nullptr;
    try {
#ifdef PIN_DEBUG
#    ifdef ESP32
        grbl_sendf(CLIENT_ALL, "Setting up pin: [%s]\r\n", str.c_str());
#    endif
#endif
        if (!parse(str, pinImplementation)) {
#ifdef ESP32
            grbl_sendf(CLIENT_ALL, "Setting up pin: '%s' failed.", str.c_str());
#endif
            return Pin::UNDEFINED;
        } else {
            // Check if we already have this pin:
            auto existingPin = Pins::PinLookup::_instance.FindExisting(pinImplementation);

            // If we already had it, and we didn't find itself, remove the new instance:
            if (existingPin >= 0) {
#ifdef ESP32
                grbl_sendf(CLIENT_ALL, "Reusing previous pin initialization.");
#endif
                if (pinImplementation) {
                    delete pinImplementation;
                }

                return Pin(uint8_t(existingPin));
            } else {
                // This is a new pin. So, register, and return the pin object that refers to it.
                auto pinNumber = pinImplementation->number();
                auto realIndex = Pins::PinLookup::_instance.SetPin(uint8_t(pinNumber), pinImplementation);
                return Pin(realIndex);
            }
        }

    } catch (AssertionFailed& ex) {  // We shouldn't get here under normal circumstances.
#ifdef PIN_DEBUG
#    ifdef ESP32
        grbl_sendf(CLIENT_ALL, "Failed. Details: %s\r\n", ex.stackTrace.c_str());
#    endif
#endif
        // RAII safety guard.
        if (pinImplementation) {
            delete pinImplementation;
        }

        return Pin::UNDEFINED;
    }
}

bool Pin::validate(const String& str) {
    Pins::PinDetail* pinImplementation;
    int              pinNumber;

    auto valid = parse(str, pinImplementation);
    if (pinImplementation) {
        delete pinImplementation;
    }

    return valid;
}

Pin Pin::ERROR(255);
Pin Pin::UNDEFINED(254);