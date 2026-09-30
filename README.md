# DoseSyncapp
DoseSync is a smart medication-management system designed to reduce missed or forgotten medicine doses.
It combines a smart physical medicine box called MedBox with a mobile application.
The basic idea is:
The app manages the medicines and schedules, while the physical MedBox handles the actual medication reminder and confirmation.
🧩 The problem
People can forget medicines because:
they don't remember the correct time,
they forget whether they already took a dose,
multiple medicines can make schedules confusing,
caregivers may not know whether a medicine was taken,
normal pillboxes don't actively remind the user.
DoseSync tries to solve these problems by making the medicine box itself interactive and connected.
📱 The DoseSync App
The user first enters their medicines into the app.
For every medicine, they can specify:
medicine name
dosage
instructions
compartment
scheduled time
whether the medicine is currently active
For example:
Paracetamol — C1 — 2 tablets — 2:30 PM
The app then synchronizes the schedule with the MedBox over Bluetooth Low Energy.
📡 The important part: the phone isn't the alarm clock
Once the schedule reaches the MedBox, the ESP32 stores the alarm information.
The MedBox has a DS3231 real-time clock, so it knows the actual time itself.
That means the user doesn't need to keep their phone connected all day.
For example:
App
 ↓
BLE
 ↓
ESP32
 ↓
Stores 14:30 alarm
 ↓
Phone can disconnect
 ↓
DS3231 reaches 14:30
 ↓
MedBox activates
This makes the physical box independent enough to function as an actual device rather than just a Bluetooth accessory.
🚨 When medicine time arrives
Suppose C1 contains Paracetamol and its scheduled time is 14:30.
At 14:30 the MedBox:
Detects the scheduled alarm.
Activates the appropriate RGB indicator.
Activates the buzzer.
Opens the main lid using the servo.
Displays the medicine information on the OLED.
The display can show something like:
ALARM RINGING
MED: Paracetamol
TIME: 14:30
COMPARTMENT C1
PRESS C1
So the user gets visual + audio + physical assistance.
💊 The user takes the medicine
The user takes the medicine from C1.
Instead of simply assuming that the dose was taken, DoseSync requires the user to press the C1 physical confirmation button.
That creates a physical confirmation event.
Take medicine
      ↓
Press C1
      ↓
ESP32 records confirmation
      ↓
Alarm becomes CONFIRMED
      ↓
Buzzer stops
      ↓
Lid closes
      ↓
Dose gets logged
This is important because the system distinguishes between:
“The alarm happened”
and
“The user confirmed the dose.”
📲 The app gets the result
After confirmation, the ESP32 can send a BLE notification back to the phone.
For example:
C1
Paracetamol
Scheduled: 14:30
Taken: 14:32
Status: Taken
The app then adds that event to the user's medication history.
So the system creates a complete chain:
Scheduled → Alarmed → Confirmed → Logged
