#ifndef AIRSPACE_VISIBILITY_HPP
#define AIRSPACE_VISIBILITY_HPP

#include "Airspace/Predicate/AirspacePredicate.hpp"

struct AirspaceComputerSettings;
struct AirspaceRendererSettings;
struct AltitudeState;

/**
 * Checks the airspace visibility settings that use the airspace type.
 */
gcc_pure
bool
IsAirspaceTypeVisible(const AbstractAirspace &airspace,
                      const AirspaceRendererSettings &renderer_settings);

/**
 * Checks the airspace visibility settings that use the aircraft
 * altitude.
 */
gcc_pure
bool
IsAirspaceAltitudeVisible(const AbstractAirspace &airspace,
                          const AltitudeState &state,
                          const AirspaceComputerSettings &computer_settings,
                          const AirspaceRendererSettings &renderer_settings);

class AirspaceVisibility {
  const AirspaceComputerSettings &computer_settings;
  const AirspaceRendererSettings &renderer_settings;
  const AltitudeState &state;

public:
  constexpr
  AirspaceVisibility(const AirspaceComputerSettings &_computer_settings,
                     const AirspaceRendererSettings &_renderer_settings,
                     const AltitudeState& _state)
    :computer_settings(_computer_settings),
     renderer_settings(_renderer_settings),
     state(_state) {}

  gcc_pure
  bool operator()(const AbstractAirspace &airspace) const;
};

class AirspaceVisiblePredicate
  :public AirspacePredicate, private AirspaceVisibility
{
public:
  AirspaceVisiblePredicate(const AirspaceComputerSettings &_computer_settings,
                           const AirspaceRendererSettings &_renderer_settings,
                           const AltitudeState& _state)
    :AirspaceVisibility(_computer_settings, _renderer_settings, _state) {}

  virtual bool operator()(const AbstractAirspace &airspace) const gcc_override;
};

#endif
