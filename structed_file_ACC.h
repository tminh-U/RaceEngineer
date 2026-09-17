#pragma once

#include <cstddef>

// Assetto Corsa Competizione shared-memory layout (v1.8.12 compatible).
// Kept in its own namespace because ACC uses the same mapping names as AC but
// extends the structures and gives several fields different semantics.
namespace acc {

#pragma pack(push, 4)

struct SPageFilePhysics
{
    int packetId;
    float gas;
    float brake;
    float fuel;
    int gear;
    int rpms;
    float steerAngle;
    float speedKmh;
    float velocity[3];
    float accG[3];
    float wheelSlip[4];
    float wheelLoad[4];
    float wheelsPressure[4];
    float wheelAngularSpeed[4];
    float tyreWear[4];
    float tyreDirtyLevel[4];
    float tyreCoreTemperature[4];
    float camberRAD[4];
    float suspensionTravel[4];
    float drs;
    float tc;
    float heading;
    float pitch;
    float roll;
    float cgHeight;
    float carDamage[5];
    int numberOfTyresOut;
    int pitLimiterOn;
    // ACC's live ABS intervention signal (0.0 = inactive, 1.0 = active).
    float abs;
    float kersCharge;
    float kersInput;
    int autoShifterOn;
    float rideHeight[2];
    float turboBoost;
    float ballast;
    float airDensity;
    float airTemp;
    float roadTemp;
    float localAngularVel[3];
    float finalFF;
    float performanceMeter;
    int engineBrake;
    int ersRecoveryLevel;
    int ersPowerLevel;
    int ersHeatCharging;
    int ersIsCharging;
    float kersCurrentKJ;
    int drsAvailable;
    int drsEnabled;
    float brakeTemp[4];
    float clutch;
    float tyreTempI[4];
    float tyreTempM[4];
    float tyreTempO[4];
    int isAIControlled;
    float tyreContactPoint[4][3];
    float tyreContactNormal[4][3];
    float tyreContactHeading[4][3];
    float brakeBias;
    float localVelocity[3];
    int P2PActivations;
    int P2PStatus;
    float currentMaxRpm;
    float mz[4];
    float fx[4];
    float fy[4];
    float slipRatio[4];
    float slipAngle[4];
    int tcInAction;
    // Retained only to preserve the documented shared-memory layout. ACC does
    // not populate this legacy compatibility field; use `abs` above instead.
    int absInAction;
    float suspensionDamage[4];
    float tyreTemp[4];
    float waterTemp;
};

// Full ACC 1.8.12 graphics-page layout. The mapping name is
// Local\\acpmf_graphics and the documented size is 1588 bytes.
struct SPageFileGraphic
{
    int packetId;
    int status;
    int session;
    wchar_t currentTime[15];
    wchar_t lastTime[15];
    wchar_t bestTime[15];
    wchar_t split[15];
    int completedLaps;
    int position;
    int iCurrentTime;
    int iLastTime;
    int iBestTime;
    float sessionTimeLeft;
    float distanceTraveled;
    int isInPit;
    int currentSectorIndex;
    int lastSectorTime;
    int numberOfLaps;
    wchar_t tyreCompound[33];
    float replayTimeMultiplier;
    float normalizedCarPosition;
    int activeCars;
    float carCoordinates[60][3];
    int carID[60];
    int playerCarID;
    float penaltyTime;
    int flag;
    int penalty;
    int idealLineOn;
    int isInPitLane;
    float surfaceGrip;
    int mandatoryPitDone;
    float windSpeed;
    float windDirection;
    int isSetupMenuVisible;
    int mainDisplayIndex;
    int secondaryDisplyIndex;
    int tc;
    int tcCut;
    int engineMap;
    int abs;
    float fuelXLap;
    int rainLights;
    int flashingLights;
    int lightStage;
    float exhaustTemperature;
    int wiperStage;
    int driverStintTotalTimeLeft;
    int driverStintTimeLeft;
    int rainTyres;
    int sessionIndex;
    float usedFuel;
    wchar_t deltaLapTime[15];
    int iDeltaLapTime;
    wchar_t estimatedLapTime[15];
    int iEstimatedLapTime;
    int isDeltaPositive;
    int iSplit;
    int isValidLap;
    float fuelEstimatedLaps;
    wchar_t trackStatus[33];
    int missingMandatoryPits;
    float clock;
    int directionLightsLeft;
    int directionLightsRight;
    int globalYellow;
    int globalYellow1;
    int globalYellow2;
    int globalYellow3;
    int globalWhite;
    int globalGreen;
    int globalChequered;
    int globalRed;
    int mfdTyreSet;
    float mfdFuelToAdd;
    float mfdTyrePressureFL;
    float mfdTyrePressureFR;
    float mfdTyrePressureRL;
    float mfdTyrePressureRR;
    int trackGripStatus;
    int rainIntensity;
    int rainIntensityIn10min;
    int rainIntensityIn30min;
    int currentTyreSet;
    int strategyTyreSet;
    int gapAhead;
    int gapBehind;
};

// The prefix is sufficient for the haptic bridge. ACC keeps these fields for
// ABI compatibility, although suspensionMaxTravel may contain zero.
struct SPageFileStatic
{
    wchar_t smVersion[15];
    wchar_t acVersion[15];
    int numberOfSessions;
    int numCars;
    wchar_t carModel[33];
    wchar_t track[33];
    wchar_t playerName[33];
    wchar_t playerSurname[33];
    wchar_t playerNick[33];
    int sectorCount;
    float maxTorque;
    float maxPower;
    int maxRpm;
    float maxFuel;
    float suspensionMaxTravel[4];
    float tyreRadius[4];
};

static_assert(offsetof(SPageFilePhysics, suspensionTravel) == 184,
    "Unexpected ACC suspensionTravel offset");
static_assert(offsetof(SPageFilePhysics, abs) == 252,
    "Unexpected ACC abs offset");
static_assert(offsetof(SPageFilePhysics, slipRatio) == 640,
    "Unexpected ACC slipRatio offset");
static_assert(offsetof(SPageFilePhysics, absInAction) == 676,
    "Unexpected ACC absInAction offset");
static_assert(offsetof(SPageFileStatic, suspensionMaxTravel) == 420,
    "Unexpected ACC static-page layout");
#ifdef _WIN32
static_assert(sizeof(SPageFileGraphic) == 1588,
    "Unexpected ACC graphics-page layout");
#endif

#pragma pack(pop)

} // namespace acc
