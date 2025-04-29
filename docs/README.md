# Final Report

## Video

{% include youtube.html id="CNu7HjIhgl4" %}

### Images

### SRS Validation

**SRS-01** - See demo video

**SRS-02** - Deprecated for reliability. We also noticed during demo days that even though people knew the slot machine was unfair, and they saw their HR was measured constantly, they would still play.

**SRS-03** :

```c
// Determine win odds based on heart rate
uint8_t determineWinOdds() {
    // Win logic according to SRS-03:
    // If HR is low, increase odds of winning
    // If HR is high, decrease odds to elongate play
    
    // Define heart rate thresholds
    #define LOW_HR_THRESHOLD 80
    #define HIGH_HR_THRESHOLD 120
    
    uint8_t winPercentage = 100;
    
    printf("HR used to determine odd: %u\n", heartRate);
    
    if (heartRate < LOW_HR_THRESHOLD) {
        // Low heart rate - guarantee win 
        winPercentage = 100;
    } else if (heartRate > HIGH_HR_THRESHOLD) {
        // High heart rate - lower odds (down to 5%)
        winPercentage = 20 - ((heartRate - HIGH_HR_THRESHOLD) / 10);
        if (winPercentage < 5) winPercentage = 5;
    } else {
        // Medium heart rate - medium odds (10-20%)
        winPercentage = 50 - ((heartRate - LOW_HR_THRESHOLD) / 2);
    }
    
    printf("Win odds: %u%%\r\n", winPercentage);
    return winPercentage;
}
```

**SRS-04** - We have multiple sound effects which are achieved via delay-based PWM, and they are initiated via functions like `play_500hz`, `play_1000hz`, `play_1500hz`, `play_750hz`, allowing us to string up notes

Our final design (... more conclusions about SRS ...)

### HRS Validation

**HRS-01** - *Insert UART pictures of different measurements*

**HRS-02** - *Insert scope pictures of different frequency waveforms*

**HRS-03** - See demo video

**HRS-04** - See pictures below

| ![HRS_04_01.jpeg](HRS_04_01.jpeg) | ![HRS_04_02.jpeg](HRS_04_02.jpeg) |
|:---: |:---: |
| Top view  | Side view  |

Our final design (... more conclusions about HRS ...)

### Conclusions

**Tony:** I believe (...)

**Theodor:** My experience (...)

### References

**TODO: insert all documentation used**

### TODOs

At the bare minimum, the page must have:

- [ ] A video of the final product

>The video must demonstrate your project’s key functionality.
>The video must be 5 minutes or less.
>Ensure your video link is accessible to the teaching team. Unlisted YouTube videos or Google Drive uploads with SEAS account access work well.
>Points will be removed if the audio quality is poor - say, if you filmed your video in a noisy electrical engineering lab.

- [ ] Images of the final product
        Include photos of your device from a few angles.
        If you have a casework, show the exterior and interior (where the good EE bits are!)

SRS Validation

- [ ] Based on your quantified system performance, comment on how you achieved or fell short of your expected software requirements.
- [ ] Validate at least two software requirements, showing how you tested and your proof of work (videos, images, logic analyzer/oscilloscope captures, etc.).

HRS Validation

- [ ] Based on your quantified system performance, comment on how you achieved or fell short of your expected hardware requirements.
- [ ] Validate at least two hardware requirements, showing how you tested and your proof of work (videos, images, logic analyzer/oscilloscope captures, etc.).

Conclusion

Reflect on your project. Some questions to address:

- [ ] What did you learn from it?
- [ ] What went well?
- [ ] What accomplishments are you proud of?
- [ ] What did you learn/gain from this experience?
- [ ] Did you have to change your approach?
- [ ] What could have been done differently?
- [ ] Did you encounter obstacles that you didn’t anticipate?
- [ ] What could be a next step for this project?
