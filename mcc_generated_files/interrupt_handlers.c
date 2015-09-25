/*
 * File:   interrupt_handlers.c
 * Author: KSK0419
 *
 * Created on September 24, 2015, 12:23 PM
 */


#include "xc.h"
#include "interrupt_handlers.h"
#include "rtcc_handler.h"

uint16_t depthBuffer[DEPTH_BUFFER_SIZE];
uint16_t batteryBuffer[BATTERY_BUFFER_SIZE];
uint16_t yAxisBuffer[Y_AXIS_BUFFER_SIZE];
uint16_t xAxisBuffer[X_AXIS_BUFFER_SIZE];

uint8_t depthBufferDepth = 0;
uint8_t batteryBufferDepth = 0;
uint8_t yAxisBufferDepth = 0;
uint8_t xAxisBufferDepth = 0;

struct tm PreviousTime;
struct tm CurrentTime;

/**
 Event Flags
 **/

bool depthBufferIsFull = false;
bool batteryBufferIsFull = false;
bool yAxisBufferIsFull = false;
bool xAxisBufferIsFull = false;
bool isMidnightPassed = false;
bool isNetlightOn = false;
bool isWaterPresent = false;

static bool prevWPSValue = false;
static bool prevSimNetlightValue = false;
static bool prevSimStatusValue = false;

void InitIOCInterrupt(void)
{
    // Set Change Notification priority to 7 (lowest))
    IPC4bits.CNIP = 0b111;
    
    // Enable change notification interrupt
    IEC1bits.CNIE = true;
    
    // Enable specific pins
    CNEN1bits.CN9IE = true; // SimStatus Change
    CNEN1bits.CN12IE = true; // SimNetlight Change
    CNEN2bits.CN27IE = true; // WPS Change
}
void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    // Clear the interrupt flag
    IFS1bits.CNIF = false;
    // Handle the interrupt
    IOCHandler();
}
void IOCHandler(void)
{
    if (wpsMeasure_GetValue() != prevWPSValue)
    {
        // We must have measured a WPS event
        UpdateWaterStatus();
    }
    
    if (simNetlight_GetValue() != prevSimNetlightValue)
    {
        // We must have measured a Netlight event
        UpdateNetStatus();
    }
    
    if (simStatus_GetValue() != prevSimStatusValue)
    {
        // Sim Status changed
        prevSimStatusValue = simStatus_GetValue();
    }
}

void UpdateWaterStatus(void)
{
    TMR2_Stop();
    // Always compare to 0
    uint16_t periodTicks = TMR2_Counter16BitGet();
    
    if (periodTicks >= WaterPeriodLowBound && 
            periodTicks <= WaterPeriodHighBound)
    {
        isWaterPresent = true;
    }
    else
    {
        isWaterPresent = false;
    }
    
    // Set the timer back to zero
    TMR2_Counter16BitSet(0);
    
    // Start it again for the next event
    TMR2_Start();
}

void UpdateNetStatus(void)
{
    TMR3_Stop();
    
    uint16_t periodTicks = TMR3_Counter16BitGet();
    
    if (periodTicks >= NetlightPeriodLowBound &&
            periodTicks <= NetlightPeriodHighBound)
    {
        isNetlightOn = true;
    }
    else
    {
        isNetlightOn = false;
    }
    
    TMR3_Counter16BitSet(0);
    
    TMR3_Start();
}

/*
 NOTE: This code is blocking.
 I don't like that, but I was unable to find a reliable way
 to make sure that the X and Y axis get sampled at near the same
 time every 10ms. I therefore deprecated ADC11 and ADC15 Handlers,
 because they are just handling the X and Y axis interrupts
 
 These are also no longer called in adc1.c during the ISR.
 */
void Timer1Handler(void)
{
    // This function starts an ADC transaction
    // To get accelerometer x and y values
    
    // It should be called every 10 ms
    

    // Get our ADC channels
    ADC1_CHANNEL xChan = ADC1_XAXIS_ACCELEROMETER;
    ADC1_CHANNEL yChan = ADC1_YAXIS_ACCELEROMETER;
    
    // Change our reference to VDD
    ADC1_ReferenceSelect(ADC1_REFERENCE_AVDD);
    
    if (!xAxisBufferIsFull)
    {
        // Set our x ADC Channel
        ADC1_ChannelSelect(xChan);
        // Turn on the ADC
        ADC1_Start();

        while (!ADC1_IsConversionComplete())
        {
            // Conversion is not complete yet
        }

        // Stop the ADC so we can deal with the buffer
        ADC1_Stop();
        
        // Load the buffer with the new result
        xAxisBuffer[xAxisBufferDepth] = ADC1_ConversionResultGet();  
        // Increment the buffer pointer
        xAxisBufferDepth++;
        
        // Check if we are full
        if (xAxisBufferDepth == X_AXIS_BUFFER_SIZE)
            xAxisBufferIsFull = true;
    }
    
    if (!yAxisBufferIsFull)
    {
        // Set our y ADC Channel
        ADC1_ChannelSelect(yChan);
        // Turn on the ADC
        ADC1_Start();
        
        while (!ADC1_IsConversionComplete())
        {
            
        }
        // Stop the ADC so we can deal with the buffer
        ADC1_Stop();
        
        // Load the buffer with the new result
        yAxisBuffer[yAxisBufferDepth] = ADC1_ConversionResultGet();
        // Increment our buffer pointer
        yAxisBufferDepth++;
        
        // check if our buffer is now full
        if (yAxisBufferDepth == Y_AXIS_BUFFER_SIZE)
            yAxisBufferIsFull = true;
    }
    
    // Switch the reference back to the band gap
    ADC1_ReferenceSelect(ADC1_REFERENCE_2VBG);
    

    
}

void Timer4Handler(void)
{
    // This function starts an ADC transaction
    // To read the battery level
    
    // It should be called every 1800 s (30 minutes))

    // Pick the battery channel
    ADC1_ChannelSelect(ADC1_BATTERY_SENSOR);
    // Select 2xVBG as reference voltage
    ADC1_ReferenceSelect(ADC1_REFERENCE_2VBG);
    
    // Start sampling, then go away, the ADC interrupt will
    //  do all of the buffering etc.
    ADC1_Start();
}

void Timer5Handler(void)
{
    // This function handlers a timer interrupt
    // It occurs every 1 second, and says that
    // we need to read the RTCC over I2C
    
    PreviousTime = CurrentTime;
    CurrentTime = GetTime();
    
    // If the day isn't the same
    if (PreviousTime.tm_mday != CurrentTime.tm_mday)
    {
        // Then trigger a midnight event
        isMidnightPassed = true;
    }
}

void ADC0Handler(void)
{
    if (depthBufferDepth == DEPTH_BUFFER_SIZE)
    {
        depthBufferIsFull = true;
    }
    else
    {
        // Save the ADC's information
        depthBuffer[depthBufferDepth] = ADC1_ConversionResultGet();
        // Increment where we are
        depthBufferDepth++;
        
        // Set a flag for the main processor to do what it wants
        // with the ADC samples
        if (depthBufferDepth == DEPTH_BUFFER_SIZE)
            depthBufferIsFull = true;
    }
}

void ADC11Handler(void)
{
    if (yAxisBufferDepth == Y_AXIS_BUFFER_SIZE)
    {
        yAxisBufferIsFull = true;
    }
    else
    {
        // Save the ADC's information
        yAxisBuffer[yAxisBufferDepth] = ADC1_ConversionResultGet();
        // Increment where we are
        yAxisBufferDepth++;
        
        // Set a flag for the main processor to do what it wants
        // with the ADC samples
        if (yAxisBufferDepth == Y_AXIS_BUFFER_SIZE)
            yAxisBufferIsFull = true;
    }   
}

void ADC12Handler(void)
{
    if (batteryBufferDepth == BATTERY_BUFFER_SIZE)
    {
        batteryBufferIsFull = true;
    }
    else
    {
        // Save the ADC's information
        batteryBuffer[batteryBufferDepth] = ADC1_ConversionResultGet();
        // Increment where we are
        batteryBufferDepth++;
        
        // Set a flag for the main processor to do what it wants
        // with the ADC samples
        if (batteryBufferDepth == BATTERY_BUFFER_SIZE)
            batteryBufferIsFull = true;
    }
}

void ADC15Handler(void)
{
    if (xAxisBufferDepth == X_AXIS_BUFFER_SIZE)
    {
        xAxisBufferIsFull = true;
    }
    else
    {
        // Save the ADC's information
        xAxisBuffer[xAxisBufferDepth] = ADC1_ConversionResultGet();
        // Increment where we are
        xAxisBufferDepth++;
        
        // Set a flag for the main processor to do what it wants
        // with the ADC samples
        if (xAxisBufferDepth == X_AXIS_BUFFER_SIZE)
            xAxisBufferIsFull = true;
    }
}
