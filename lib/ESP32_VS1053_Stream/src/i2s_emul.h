#include <Arduino.h>

class I2S_emul
{
private:
    /* data */
public:
    I2S_emul(/* args */);
    ~I2S_emul();

    void begin();
    void loadDefaultVs1053Patches();
    void switchToMp3Mode();
    void setVolume(uint8_t vol);
    void startSong();
    void playChunk(uint8_t *data, size_t len);
    bool data_request();
    void stopSong();
};
