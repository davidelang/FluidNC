#include "MotorUnit.h"
#include "../Report.h"


#define P 300 //260
#define I 0
#define D 0

#define TCAADDR 0x70


void MotorUnit::begin(int forwardPin,
               int backwardPin,
               int readbackPin,
               int encoderAddress,
               int channel1,
               int channel2){

    _encoderAddress = encoderAddress;

    Wire.begin(5,4, 200000);
    I2CMux.begin(TCAADDR, Wire);
    I2CMux.setPort(_encoderAddress);
    encoder.begin();
    zero();

    motor.begin(forwardPin, backwardPin, readbackPin, channel1, channel2);

    positionPID.setPID(P,I,D);
    positionPID.setOutputLimits(-1023,1023);

    
}

void MotorUnit::zero(){
    I2CMux.setPort(_encoderAddress);
    encoder.resetCumulativePosition();
}

/*!
 *  @brief  Sets the target location
 */
void MotorUnit::setTarget(double newTarget){
    // if(abs(newTarget - setpoint) > 1){
    //     log_info("Step change in target detected on " << _encoderAddress);
    //     log_info("Old target: " << setpoint);
    //     log_info("New target: " << newTarget);
    // }
    setpoint = newTarget;
}

/*!
 *  @brief  Gets the target location
 */
double MotorUnit::getTarget(){
    return setpoint;
}

/*!
 *  @brief  Sets the position of the cable
 */
int MotorUnit::setPosition(double newPosition){
    int angleTotal = (newPosition*4096)/_mmPerRevolution;
    I2CMux.setPort(_encoderAddress);
    encoder.resetCumulativePosition(angleTotal);

    return true;
}

/*!
 *  @brief  Reads the current position of the axis
 */
double MotorUnit::getPosition(){
    double positionNow = (mostRecentCumulativeEncoderReading/4096.0)*_mmPerRevolution*-1;

    // if(abs(positionNow - _lastPosition) > 1){
    //     log_info("Position jump detected on "  << _encoderAddress << " of " << positionNow - _lastPosition);
    //     int timeElapsed = millis() - lastCallGetPos;
    //     log_info("Time since last call: " << timeElapsed);
    // }
    // lastCallGetPos = millis();
    // _lastPosition = positionNow;

    return positionNow;
}

/*!
 *  @brief  Gets the current motor power draw
 */
double MotorUnit::getCurrent(){
    return motor.readCurrent();
}

/*!
 *  @brief  Computes and returns the error in the axis positioning
 */
double MotorUnit::getError(){
    
    double errorDist = setpoint - getPosition();
    
    return errorDist;
    
}

/*!
 *  @brief  Stops the motor
 */
void MotorUnit::stop(){
    motor.stop();
}

//---------------------Functions related to maintaining the PID controllers-----------------------------------------



/*!
 *  @brief  Reads the encoder value and updates it's position and measures the velocity since the last call
 */
void MotorUnit::updateEncoderPosition(){

    I2CMux.setPort(_encoderAddress);

    if(encoder.isConnected()){
        mostRecentCumulativeEncoderReading = encoder.getCumulativePosition(); //This updates and returns the encoder value
    }
    else if(millis() - encoderReadFailurePrintTime > 1000){
        encoderReadFailurePrintTime = millis();
        log_info("Encoder read failure on " << _encoderAddress);
    }
}

/*!
 *  @brief  Recomputes the PID and drives the output
 */
double MotorUnit::recomputePID(){
    
    _commandPWM = positionPID.getOutput(getPosition(),setpoint);

    motor.runAtPWM(_commandPWM);

    return _commandPWM;

}

/*
*  @brief  Gets the last command PWM
*/
double MotorUnit::getCommandPWM(){
    return _commandPWM;
}

/*!
 *  @brief  Runs the motor to extend for a little bit to put some slack into the coiled belt. Used to make it easier to extend.
 */
void MotorUnit::decompressBelt(){
    unsigned long time = millis();
    unsigned long elapsedTime = millis()-time;
    while(elapsedTime < 500){
        elapsedTime = millis()-time;
        motor.fullOut();
    }
}

/*!
 *  @brief  Sets the motor to comply with how it is being pulled
 */
bool MotorUnit::comply(unsigned long *timeLastMoved, double *lastPosition, double *amtToMove, double maxSpeed){
    
    //Update position and PID loop
    recomputePID();
    
    //If we've moved any, then drive the motor outwards to extend the belt
    float positionNow = getPosition();
    float distMoved = positionNow - *lastPosition;

    //If the belt is moving out, let's keep it moving out
    if( distMoved > .001){
        //Increment the target
        setTarget(positionNow + *amtToMove);
        
        *amtToMove = *amtToMove + 1;
        
        *amtToMove = min(*amtToMove, maxSpeed);
        
        //Reset the last moved counter
        *timeLastMoved = millis();
    
    //If the belt is moving in we need to stop it from moving in
    }else if(distMoved < -.04){
        *amtToMove = 0;
        setTarget(positionNow + .1);
        stop();
    }
    //Finally if the belt is not moving we want to spool things down
    else{
        *amtToMove = *amtToMove / 2;
        setTarget(positionNow);
        stop();
    }
    

    *lastPosition = positionNow;

    //Return indicates if we have moved within the timeout threshold
    if(millis()-*timeLastMoved > 5000){
        return false;
    }
    else{
        return true;
    }
}

/*!
 *  @brief  Fully retracts this axis and zeros it out or if it is already retracted extends it to the targetLength
 */
bool MotorUnit::retract(double targetLength){
    
    log_info("Retracting");

    int absoluteCurrentThreshold = 1900;
    int incrementalThreshold = 75;
    int incrementalThresholdHits = 0;
    float alpha = .2;
    float baseline = 700;

    uint16_t speed = 0;

    //Keep track of the elapsed time
    unsigned long time = millis();
    unsigned long elapsedTime = 0;
    
    //Pull until taught
    while(true){
        
        //Gradually increase the pulling speed
        if(random(0,4) == 2){ //This is a hack to make it speed up more slowly because we can't add less than 1 to an int
            speed = min(speed + 1, 1023);
        }
        motor.backward(speed);

        //When taught
        int currentMeasurement = motor.readCurrent();

        //_webPrint(0xFF,"Current: %i, Baseline: %f, difference: %f \n", currentMeasurement, baseline, currentMeasurement - baseline);
        baseline = alpha * float(currentMeasurement) + (1-alpha) * baseline;

        if(currentMeasurement - baseline > incrementalThreshold){
            incrementalThresholdHits = incrementalThresholdHits + 1;
        }
        else{
            incrementalThresholdHits = 0;
        }

        if(currentMeasurement > absoluteCurrentThreshold || incrementalThresholdHits > 4){
            motor.stop();

            //Print how much the length of the belt changed compared to memory
            //_webPrint(0xFF,"Belt position after retract: %f\n", getPosition());
            log_info("Belt positon after retract: ");
            log_info(getPosition());

            zero();
            
            //If we hit the current limit immediately because there wasn't any slack we will extend
            elapsedTime = millis()-time;
            if(elapsedTime < 1500){
                
                //Extend some belt to get things started
                decompressBelt();
                
                unsigned long timeLastMoved = millis();
                double lastPosition = getPosition();
                double amtToMove = 0.1;
                
                while(getPosition() < targetLength){
                    //Check for timeout
                    if(!comply(&timeLastMoved, &lastPosition, &amtToMove, 500)){//Comply updates the encoder position and does the actual moving
                        
                        //Stop and return
                        setTarget(getPosition());
                        motor.stop();
                        
                        return false;
                    }
                    
                    // Delay without blocking
                    unsigned long time = millis();
                    unsigned long elapsedTime = millis()-time;
                    while(elapsedTime < 50){
                        elapsedTime = millis()-time;
                    }
                }
                
                //Position hold for 2 seconds to make sure we are in the right place
                setTarget(targetLength);
                time = millis();
                elapsedTime = millis()-time;
                while(elapsedTime < 500){
                    elapsedTime = millis()-time;
                    recomputePID();
                }
                
                motor.stop();

                log_info("Belt positon after extend: ");
                log_info(getPosition());
                log_info("Expected measured: ");
                log_info(getPosition() + 153.4);
                return true;
            }
            else{
                return false;
            }
        }
    }
}
