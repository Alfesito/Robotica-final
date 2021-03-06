/******************* INCLUDES ******************/
/***********************************************/

/******************** General ******************/
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <sys/time.h>
#include <iostream>

/******************** Simulator ****************/
/******************** Sensors ******************/
#include "epuckproximitysensor.h"
#include "contactsensor.h"
#include "lightsensor.h"
#include "bluelightsensor.h"
#include "redlightsensor.h"
#include "groundsensor.h"
#include "groundmemorysensor.h"
#include "redbatterysensor.h"
#include "reallightsensor.h"
#include "realbluelightsensor.h"
#include "realredlightsensor.h"

/******************** Actuators ****************/
#include "wheelsactuator.h"

/******************** Controller **************/
#include "subsumptionlightcontroller.h"


/******************************************************************************/
/******************************************************************************/

extern gsl_rng* rng;
extern long int rngSeed;
/******************************************************************************/
/******************************************************************************/

using namespace std;
/******************************************************************************/
/******************************************************************************/

#define BEHAVIORS 6

#define AVOID_PRIORITY 0
#define RELOAD_PRIORITY 1
#define LED_PRIORITY 2
#define BLUE_LED_PRIORITY 3
#define FORAGE_PRIORITY	4
#define NAVIGATE_PRIORITY 5


#define PROXIMITY_THRESHOLD 0.6
#define BATTERY_THRESHOLD 0.5

#define SPEED 1000.0


/******************************************************************************/
/******************************************************************************/
CSubsumptionLightController::CSubsumptionLightController (const char* pch_name, CEpuck* pc_epuck, int n_write_to_file) : CController (pch_name, pc_epuck)

{
	/* Set Write to File */
	m_nWriteToFile = n_write_to_file;	
	/* Set epuck */
	m_pcEpuck = pc_epuck;
	/* Set Wheels */
	m_acWheels = (CWheelsActuator*) m_pcEpuck->GetActuator(ACTUATOR_WHEELS);
	/* Set Prox Sensor */
	m_seProx = (CEpuckProximitySensor*) m_pcEpuck->GetSensor(SENSOR_PROXIMITY);
	/* Set contact Sensor */
	m_seContact = (CContactSensor*) m_pcEpuck->GetSensor (SENSOR_CONTACT);
	/* Set ground Sensor */
	m_seGround = (CGroundSensor*) m_pcEpuck->GetSensor (SENSOR_GROUND);
	/* Set ground memory Sensor */
	m_seGroundMemory = (CGroundMemorySensor*) m_pcEpuck->GetSensor (SENSOR_GROUND_MEMORY);
	/* Set red battery Sensor */
	m_seRedBattery = (CRedBatterySensor*) m_pcEpuck->GetSensor (SENSOR_RED_BATTERY);
	/* Set light Switch Sensor */
	m_seLight = (CLightSensor*) m_pcEpuck->GetSensor(SENSOR_LIGHT);
	/* Set blue light Switch Sensor */
        m_seBlueLight =(CBlueLightSensor*) m_pcEpuck->GetSensor(SENSOR_BLUE_LIGHT);
	/* Set red light sensor*/
	m_seRedLight = (CRealRedLightSensor*) m_pcEpuck->GetSensor(SENSOR_REAL_RED_LIGHT);

	/* Initilize Variables */
	m_fLeftSpeed = 0.0;
	m_fRightSpeed = 0.0;
	m_NLight = 0.0;
	m_NBlueLight = 0.0;
	memory=0.0;
	memoryanterior = 0.0;
	totalrecogidos = 0.0;
	objetorecogido = 0.0;
	/* Create TABLE for the COORDINATOR */
	m_fActivationTable = new double* [BEHAVIORS];
	for ( int i = 0 ; i < BEHAVIORS ; i++ )
	{
		m_fActivationTable[i] = new double[3];
	}
}

/******************************************************************************/
/******************************************************************************/

CSubsumptionLightController::~CSubsumptionLightController()
{
	for ( int i = 0 ; i < BEHAVIORS ; i++ )
	{
		delete [] m_fActivationTable;
	}
}

/******************************************************************************/
	/***AQUI EMPIEZA EL CODIGO A PROGRAMAR***/

void CSubsumptionLightController::SimulationStep(unsigned n_step_number, double f_time, double f_step_interval)
{
	
	/* Move time to global variable, so it can be used by the bahaviors to write to files*/
	m_fTime = f_time;

	/* Execute the levels of competence */
	ExecuteBehaviors();

	/* Execute Coordinator */
	Coordinator();

	/* Set Speed to wheels */
	m_acWheels->SetSpeed(m_fLeftSpeed, m_fRightSpeed);

	if (m_nWriteToFile ) 
	{
	/* INIT: WRITE TO FILES */
	/* Write robot position and orientation */
		FILE* filePosition = fopen("outputFiles/robotPosition", "a");
		fprintf(filePosition,"%2.4f %2.4f %2.4f %2.4f\n", m_fTime, m_pcEpuck->GetPosition().x, m_pcEpuck->GetPosition().y, m_pcEpuck->GetRotation());
		fclose(filePosition);

		/* Write robot wheels speed */
		FILE* fileWheels = fopen("outputFiles/robotWheels", "a");
		fprintf(fileWheels,"%2.4f %2.4f %2.4f %2.4f %2.4f\n", m_fTime, m_fLeftSpeed, m_fRightSpeed, m_NLight, m_NBlueLight);
		fclose(fileWheels);
		/* END WRITE TO FILES */
	}
}

/******************************************************************************/
/******************************************************************************/

void CSubsumptionLightController::ExecuteBehaviors ( void )
{
	for ( int i = 0 ; i < BEHAVIORS ; i++ )
	{
		m_fActivationTable[i][2] = 0.0;
	}
	/* Release Inhibitors */
	fBattInhibitor = 1.0;
	/* Set Leds to BLACK */
	m_pcEpuck->SetAllColoredLeds(LED_COLOR_BLACK);
	ObstacleAvoidance ( AVOID_PRIORITY );
  	GoLoad ( RELOAD_PRIORITY );
	SwitchLight(LED_PRIORITY);
	SwitchBlueLight(BLUE_LED_PRIORITY);
	Forage( FORAGE_PRIORITY );	
	Navigate ( NAVIGATE_PRIORITY );
}

/******************************************************************************/
/******************************************************************************/

void CSubsumptionLightController::Coordinator ( void )
{
	int nBehavior;
	for ( nBehavior = 0 ; nBehavior < BEHAVIORS ; nBehavior++ )
	{
		if ( m_fActivationTable[nBehavior][2] == 1.0 )
		{	
			break;
		}
	}

	m_fLeftSpeed = m_fActivationTable[nBehavior][0];
	m_fRightSpeed = m_fActivationTable[nBehavior][1];
	
  printf("%d %2.4f %2.4f %1.1f %1.0f %1.0f %1.1f %1.1f %1.1f \n", nBehavior, m_fLeftSpeed, m_fRightSpeed,fBattInhibitor, m_NLight,m_NBlueLight, memory, objetorecogido,totalrecogidos );
	printf("\n");	

  if (m_nWriteToFile ) 
	{
		// INIT: WRITE TO FILES 
		// Write coordinator ouputs 
		FILE* fileOutput = fopen("outputFiles/coordinatorOutput", "a");
		fprintf(fileOutput,"%2.4f %d %2.4f %2.4f %2.4f  \n", m_fTime, nBehavior, m_fLeftSpeed, m_fRightSpeed,m_NLight);
		fclose(fileOutput);
		// END WRITE TO FILES 
	}
}

/******************************************************************************/
/******************************************************************************/

void CSubsumptionLightController::SwitchBlueLight( unsigned int un_priority ){
	
	double* groundMemory = m_seGroundMemory->GetSensorReading(m_pcEpuck);
	double* bluelight = m_seBlueLight->GetSensorReading(m_pcEpuck);
	totalBlueLight = 0;
	TOTBlueLight = 0;
	totalBlueLight = bluelight[0]+bluelight[7];
	TOTBlueLight = bluelight[0]+bluelight[1]+bluelight[2]+bluelight[3]+bluelight[4]+bluelight[5]+bluelight[6]+bluelight[7];
	memory=groundMemory[0];
	
	if (fBattInhibitor == 1.0 && m_NLight==5 ){
			//m_pcEpuck->SetAllColoredLeds(LED_COLOR_YELLOW);
			if ( totalBlueLight >= 0.8)
			{
				m_seBlueLight->SwitchNearestLight(0);
				m_NBlueLight=m_NBlueLight+1;
				m_pcEpuck->SetAllColoredLeds(LED_COLOR_GREEN);
				m_fActivationTable[un_priority][2] = 1.0;
				m_fActivationTable[un_priority][0] = SPEED;
				m_fActivationTable[un_priority][1] = SPEED;
			}
			//m_pcEpuck->SetAllColoredLeds(LED_COLOR_YELLOW);
			/* GO Light */
			if ( (bluelight[0] * bluelight[7] == 0.0) && (TOTBlueLight != 0))
			{
				
				/* Calc light intensity at the left and right */
				double bluelightLeft = bluelight[0] + bluelight[1] + bluelight[2] + bluelight[3]; //sensores por la izquierda
				double bluelightRight= bluelight[4] + bluelight[5] + bluelight[6] + bluelight[7]; // sensores por la derecha

				/* If light on the left */
				if ( bluelightLeft > bluelightRight )
				{
					//if(){
						
					//}
					/* Turn left */
					//m_acWheels->SetSpeed(-500,500); 	// gira a la izquierda
					m_fActivationTable[un_priority][2] = 1.0;
					m_fActivationTable[un_priority][0] = -SPEED;
					m_fActivationTable[un_priority][1] = SPEED;
					
				}
				else if(bluelightLeft < bluelightRight)
				{
					/* Turn right */
					//m_acWheels->SetSpeed(500,-500);		// gira a la derecha
					m_fActivationTable[un_priority][2] = 1.0;
					m_fActivationTable[un_priority][0] = SPEED;
					m_fActivationTable[un_priority][1] = -SPEED;
				}
				else{	//recto
					m_fActivationTable[un_priority][2] = 1.0;
					m_fActivationTable[un_priority][0] = SPEED;
					m_fActivationTable[un_priority][1] = SPEED;
				}
			}
				
			if((bluelight[0] * bluelight[7] != 0.0) && (TOTBlueLight != 0)){
				m_fActivationTable[un_priority][2] = 1.0;
				m_fActivationTable[un_priority][0] = SPEED;
				m_fActivationTable[un_priority][1] = SPEED;
			}
		
	}
}
/**FUNCION AÑADIDA**/
void CSubsumptionLightController::SwitchLight( unsigned int un_priority ){

	double* light = m_seLight->GetSensorReading(m_pcEpuck);
	double* groundMemory = m_seGroundMemory->GetSensorReading(m_pcEpuck);

	totalLight = 0;
	TOTLight = 0;
	
  	totalLight = light[0]+light[7];
	TOTLight = light[0]+light[1]+light[2]+light[3]+light[4]+light[5]+light[6]+light[7];
	memory=groundMemory[0];

	if (fBattInhibitor == 1.0 ){

			if ( totalLight >= 0.8)
			{
				m_seLight->SwitchNearestLight(0);
				m_pcEpuck->SetAllColoredLeds(LED_COLOR_GREEN);
				m_fActivationTable[un_priority][0] = SPEED;
				m_fActivationTable[un_priority][1] = SPEED;
				m_fActivationTable[un_priority][2] = 1.0;
				m_NLight = m_NLight +1;
			}
			//m_pcEpuck->SetAllColoredLeds(LED_COLOR_YELLOW);
			/* GO Light */
			if ( (light[0] * light[7] == 0.0) && (TOTLight != 0))
			{
				
				/* Calc light intensity at the left and right */
				double lightLeft 	= light[0] + light[1] + light[2] + light[3]; //sensores por la izquierda
				double lightRight = light[4] + light[5] + light[6] + light[7];		// sensores por la derecha

				/* If light on the left */
				if ( lightLeft > lightRight )
				{
					/* Turn left */
					//m_acWheels->SetSpeed(-500,500); 	// gira a la izquierda
					m_fActivationTable[un_priority][2] = 1.0;
					m_fActivationTable[un_priority][0] = -SPEED;
					m_fActivationTable[un_priority][1] = SPEED;
					
				}
				else{
					/* Turn right */
					//m_acWheels->SetSpeed(500,-500);		// gira a la derecha
					m_fActivationTable[un_priority][2] = 1.0;
					m_fActivationTable[un_priority][0] = SPEED;
					m_fActivationTable[un_priority][1] = -SPEED;
				}
			}
			if((light[0] * light[7] != 0.0) && (TOTLight != 0)){
				m_fActivationTable[un_priority][2] = 1.0;
				m_fActivationTable[un_priority][0] = SPEED;
				m_fActivationTable[un_priority][1] = SPEED;
			}			
		
	}
	/* INIT WRITE TO FILE */

}



void CSubsumptionLightController::ObstacleAvoidance ( unsigned int un_priority )
{
	double* groundMemory = m_seGroundMemory->GetSensorReading(m_pcEpuck);
	/* Leer Sensores de Proximidad */
	double* prox = m_seProx->GetSensorReading(m_pcEpuck);

	double fMaxProx = 0.0;
	const double* proxDirections = m_seProx->GetSensorDirections();
	double* light = m_seLight->GetSensorReading(m_pcEpuck);	//añadir

	dVector2 vRepelent;
	vRepelent.x = 0.0;
	vRepelent.y = 0.0;
	memory=groundMemory[0];
	/* Calc vector Sum */
	for ( int i = 0 ; i < m_seProx->GetNumberOfInputs() ; i ++ )
	{
		vRepelent.x += prox[i] * cos ( proxDirections[i] );
		vRepelent.y += prox[i] * sin ( proxDirections[i] );

		if ( prox[i] > fMaxProx )
			fMaxProx = prox[i];
	}
	
	/* Calc pointing angle */
	float fRepelent = atan2(vRepelent.y, vRepelent.x);
	/* Create repelent angle */
	fRepelent -= M_PI;
	/* Normalize angle */
	while ( fRepelent > M_PI ) fRepelent -= 2 * M_PI;
	while ( fRepelent < -M_PI ) fRepelent += 2 * M_PI;
	
	/* If above a threshold */
	if ( fMaxProx > PROXIMITY_THRESHOLD )
	{
		/* Set Leds to GREEN */
		m_pcEpuck->SetAllColoredLeds(	LED_COLOR_GREEN);


		double fCLinear = 1.0;
		double fCAngular = 1.0;
		double fC1 = SPEED / M_PI;
		
		/* Calc Linear Speed */
		double fVLinear = SPEED * fCLinear * ( cos ( fRepelent / 2) );

		/*Calc Angular Speed */
		double fVAngular = fRepelent;

		m_fActivationTable[un_priority][0] = fVLinear - fC1 * fVAngular;
		m_fActivationTable[un_priority][1] = fVLinear + fC1 * fVAngular;
		m_fActivationTable[un_priority][2] = 1.0;
	}

	if (m_nWriteToFile ) 
	{
		/* INIT WRITE TO FILE */
		/* Write level of competence ouputs */
		FILE* fileOutput = fopen("outputFiles/avoidOutput", "a");
		fprintf(fileOutput, "%2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f ", m_fTime, prox[0], prox[1], prox[2], prox[3], prox[4], prox[5], prox[6], prox[7], fMaxProx, fRepelent);
		fprintf(fileOutput, "%2.4f %2.4f %2.4f\n",m_fActivationTable[un_priority][2], m_fActivationTable[un_priority][0], m_fActivationTable[un_priority][1]);
		fclose(fileOutput);
		/* END WRITE TO FILE */
	}
}

/******************************************************************************/
/******************************************************************************/

void CSubsumptionLightController::Navigate ( unsigned int un_priority )
{	
	double* groundMemory = m_seGroundMemory->GetSensorReading(m_pcEpuck);
	memory=groundMemory[0];
	if (fBattInhibitor == 1.0){	
		m_fActivationTable[un_priority][0] = SPEED;
		m_fActivationTable[un_priority][1] = SPEED;
		
	}	
	m_fActivationTable[un_priority][2] = 1.0;

	
	if (m_nWriteToFile ) 
	{
		// INIT: WRITE TO FILES 
		// Write level of competence ouputs 
		FILE* fileOutput = fopen("outputFiles/navigateOutput", "a");
		fprintf(fileOutput,"%2.4f %2.4f %2.4f %2.4f \n", m_fTime, m_fActivationTable[un_priority][2], m_fActivationTable[un_priority][0], m_fActivationTable[un_priority][1]);
		fclose(fileOutput);
		// END WRITE TO FILES 
	}
	
}
		
/******************************************************************************/
/******************************************************************************/

void CSubsumptionLightController::GoLoad ( unsigned int un_priority )
{
	double* groundMemory = m_seGroundMemory->GetSensorReading(m_pcEpuck);
	/* Leer Battery Sensores */
	double* battery = m_seRedBattery->GetSensorReading(m_pcEpuck);

	/* Leer Sensores de Luz */
	double* light = m_seRedLight->GetSensorReading(m_pcEpuck);

	/* Calc light intensity at the left and right */
	double lightLeft 	= light[0] + light[1] + light[2] + light[3];
	double lightRight = light[4] + light[5] + light[6] + light[7];
	memory=groundMemory[0];
	/* If battery below a BATTERY_THRESHOLD */
	if ( battery[0] < BATTERY_THRESHOLD ){
		/* Set Leds to RED */
		m_pcEpuck->SetAllColoredLeds(LED_COLOR_RED);
		fBattInhibitor = 0.0;
		/* If not pointing to the light */
		if ( light[0] * light[7] == 0.0 )
		{
			/* Activate level of competence */
			m_fActivationTable[un_priority][2] = 1.0;

			/* If light on the left */
			if ( lightLeft > lightRight ){
				/* Turn left */
				m_fActivationTable[un_priority][0] = -SPEED;
				m_fActivationTable[un_priority][1] = SPEED;
					
			}else{
				/* Turn right */
				m_fActivationTable[un_priority][0] = SPEED;
				m_fActivationTable[un_priority][1] = -SPEED;
			}
		}else if(light[0] * light[7] != 0.0){
			m_fActivationTable[un_priority][2] = 1.0;
			m_fActivationTable[un_priority][0] = SPEED;
			m_fActivationTable[un_priority][1] = SPEED;
		}
	}else{
		fBattInhibitor = 1.0;	
	}	

	if (m_nWriteToFile ){
		/* INIT WRITE TO FILE */
		FILE* fileOutput = fopen("outputFiles/batteryOutput", "a");
		fprintf(fileOutput, "%2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f %2.4f ", m_fTime, battery[0], light[0], light[1], light[2], light[3], light[4], light[5], light[6], light[7]);
		fprintf(fileOutput, "%2.4f %2.4f %2.4f\n",m_fActivationTable[un_priority][2], m_fActivationTable[un_priority][0], m_fActivationTable[un_priority][1]);
		fclose(fileOutput);
		/* END WRITE TO FILE */
	}
}


void CSubsumptionLightController::Forage ( unsigned int un_priority )
{
	

	/* Leer Sensores de Suelo Memory */
	double* groundMemory = m_seGroundMemory->GetSensorReading(m_pcEpuck);
	
	// If with a virtual puck 
	if(fBattInhibitor==1.0){
		if(memory == 1.0 && memoryanterior == 0.0){
	objetorecogido = objetorecogido + 1 ;
	memoryanterior = 1.0;
}
		if ( memory == 1.0 ){
			// Set Leds to BLUE
			m_pcEpuck->SetAllColoredLeds(LED_COLOR_BLUE);	
			m_fActivationTable[un_priority][0] = SPEED;
			m_fActivationTable[un_priority][1] = SPEED;			
			m_fActivationTable[un_priority][2] = 1.0;					
		}
		if(memory == 0.0 && memoryanterior == 1.0){
	objetorecogido = objetorecogido - 1 ;
	memoryanterior = 0.0;
	totalrecogidos = totalrecogidos +1;
}	
	
	
	}
	/*if(fBattInhibitor==1.0 &&memory==0.0 && nObject==1.0){
		m_pcEpuck->SetAllColoredLeds(LED_COLOR_YELLOW);
	}
	else if(fBattInhibitor==1.0 && memory==1.0){	
		m_pcEpuck->SetAllColoredLeds(LED_COLOR_BLUE);	
		m_fActivationTable[un_priority][0] = SPEED;
		m_fActivationTable[un_priority][1] = SPEED;			
		m_fActivationTable[un_priority][2] = 1.0;
		nObject==1.0;
	}*/
	
	
		
	
	
	//FILE
}

