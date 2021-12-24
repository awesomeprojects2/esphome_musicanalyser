#include "esphome.h"
#include "arduinoFFT.h"
#include "Adafruit_SSD1306.h"
#include "Adafruit_GFX.h"
#include "StopWatch.h"

//Inspired by: https://github.com/s-marley/ESP32_FFT_VU/blob/master/ESP32_FFT_VU/ESP32_FFT_VU.ino
class AudioAnalyser : public Component, public Sensor {
    public:
        Sensor *band0 = new Sensor();
        Sensor *band1 = new Sensor();
        Sensor *band2 = new Sensor();
        Sensor *band3 = new Sensor();
        Sensor *band4 = new Sensor();
        Sensor *band5 = new Sensor();
        Sensor *band6 = new Sensor();

        Sensor *bandSensors[7] = { band0, band1, band2, band3, band4, band5, band6 };
        // in band we will add all values of the bands
        #define SAMPLES             512         // Must be a power of 2
        #define SAMPLING_FREQ       40000        // Hz, must be 40000 or less due to ADC conversion time. Determines maximum frequency that can be analysed by the FFT Fmax=sampleF/2.
        #define AUDIO_IN_PIN        36           // Signal in on this pin
        #define NOISE               2000         // Used as a crude noise filter, values below this are ignored
        #define TOP                 100
        #define SCREEN_WIDTH        128
        #define SCREEN_HEIGHT       32
        #define SCREEN_RESET_PIN    -1

        //a sampling action will take about 12.8 ms.
        // Then there is some display stuff happening over I2C which will delay a lot more
        // The PUBLISH_INTERVAL sets the amount of loops needs to be taken before publishing.
        #define PUBLISH_INTERVAL    1       

        const double amplitudes[7] = { 500, 500, 500, 200, 200, 200, 200 };
        const int dividers[7] = { 2, 2, 2, 8, 15, 23, 147 };
        int bandValues[7] = {0, 0, 0, 0, 0, 0, 0};
        int peakValues[7] = {0, 0, 0, 0, 0, 0, 0};
        int peakMemValues[7] = {0, 0, 0, 0, 0, 0, 0};

        bool displayEnabled = false;
        int publicCounter = 0;
        double vReal[SAMPLES];
        double vImag[SAMPLES];
        unsigned int sampling_period_us;
        arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);
        Adafruit_SSD1306 display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET_PIN);
        StopWatch MySW = StopWatch();

        void setup() override {
            ESP_LOGI("AudioAnalyser", "setup");
            sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQ));
            displayEnabled = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
            if (! displayEnabled)
                ESP_LOGE("AudioAnalyser", "Display Error");
            if (displayEnabled)
            {
                display.clearDisplay();
                display.display();
            }
            MySW.setResolution(StopWatch::MICROS);
        }

        double GetValue(int bar)
        {
            int barHeight = peakValues[bar];
            if (barHeight > TOP) barHeight = TOP;
            barHeight /= 20;
            barHeight *= 20;
            return barHeight; //results in values [ 0, 20, 40, 60, 80, 100 ] 
        }

        void publish()
        {
            for (byte band = 0; band <= 6; band++)
            {
                int temp = GetValue(band);
                if (temp != peakMemValues[band])    //not changed no need to publish
                {
                    bandSensors[band]->publish_state(temp);
                    peakMemValues[band] = temp;
                }
                peakValues[band] = 0;
            }  
        }

        void loop() override {
            // Sample the audio pin
            for (int i = 0; i < SAMPLES; i++) {
                MySW.reset();
                MySW.start();
                vReal[i] = analogRead(AUDIO_IN_PIN); // A conversion takes about 9.7uS on an ESP32 
                vImag[i] = 0;
                while (MySW.elapsed() < sampling_period_us) { /* do nothing to wait */ }
                MySW.stop();
            }
            // Compute FFT
            FFT.DCRemoval();
            FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
            FFT.Compute(FFT_FORWARD);
            FFT.ComplexToMagnitude();
            // Analyse FFT results
            for (int i = 2; i < (SAMPLES/2); i++){ // Don't use sample 0 and only first SAMPLES/2 are usable. Each array eleement represents a frequency and its value the amplitude.
                if (vReal[i] > NOISE) 
                { // Add a crude noise filter, 10 x amplitude or more
                    if (i<=3 )              addValue(0, (int)vReal[i]); // 125Hz
                    if (i >3   && i<=5 )    addValue(1, (int)vReal[i]); // 250Hz
                    if (i >5   && i<=7 )   addValue(2, (int)vReal[i]); // 500Hz
                    if (i >7  && i<=15 )   addValue(3, (int)vReal[i]); // 1000Hz
                    if (i >15  && i<=30 )   addValue(4, (int)vReal[i]); // 2000Hz
                    if (i >30  && i<=53 )  addValue(5, (int)vReal[i]); // 4000Hz
                    if (i >53  && i<=200 ) addValue(6, (int)vReal[i]); // 8000Hz
                }
            }
            display.clearDisplay();
            for (byte band = 0; band <= 6; band++)
            {
                double bandValue =  bandValues[band]/dividers[band];
                displayBand(band, bandValue);
                if (peakValues[band] < bandValue)
                    peakValues[band] = bandValue;
                bandValues[band] = 0;
            }              
            display.display();
            publicCounter++;
            if (publicCounter > PUBLISH_INTERVAL)
            {
                publish();
                publicCounter = 0;
            }
        }   
        
        void addValue(int band, int dsize)
        {
            bandValues[band] += (int)(dsize / amplitudes[band]);
        }

        void displayBand(int band, int dsize){
            int dmax = 64;
            if (dsize > dmax) dsize = dmax;
            for (int s = 0; s <= dsize; s=s+2)
            {
                display.drawLine(18*band, 64-s, (18*band)+14, 64-s, SSD1306_WHITE);
            }
        }


};