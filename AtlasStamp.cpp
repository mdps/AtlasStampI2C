#include "AtlasStamp.h"

//TODO: Parametro unit_len deprecated? se peude cambiar en el malloc por strlen()?
AtlasStamp::AtlasStamp(uint8_t address, char* unit, uint8_t unit_len, float min_value, float max_value, uint8_t max_num_fields_in_response) :
	_address(address),
	_max_response_field_count(max_num_fields_in_response),
	_response_field_count(max_num_fields_in_response),
	_last_result{ (float*)malloc(sizeof(float) * _response_field_count) },
	_is_init(false),
	_is_busy(false),
	_async_comand_ready_by(0),
	_min_value(min_value),
	_max_value(max_value),
	_unit{ (char*)malloc(sizeof(char) * (unit_len+1)) },
	stamp_version{ '0','.','0','\0' },
	is_awake(true)
{
	//Inicialize the sensor unit
	strcpy(_unit, unit);
	_clean_buffer();
}

//uint8_t AtlasStamp::raw_command(char* cmd, unsigned long timeout)
//{
//	return _raw_command(cmd, timeout);
//}

void AtlasStamp::_resize_response_count(uint8_t count)
{
	if (count == 0) { count = 1; }
	else if (count > _max_response_field_count) { count = _max_response_field_count; }

	if (count == _response_field_count)
	{
#ifdef ATLAS_DEBUG
		Serial.printf("AtlasStamp::_resize_response_count() cant reallocate, already have [%d] fields\n", _response_field_count);
#endif
		return;
	}

#ifdef ATLAS_DEBUG
	Serial.printf("AtlasStamp::_resize_response_count() reallocated space for sensor readings from[%d] to[%d] floats\n", _response_field_count, count);
#endif
	_response_field_count = count;
	_last_result = (float*)realloc(_last_result, sizeof(float) * _response_field_count);
}

bool AtlasStamp::_command_async(char* cmd, unsigned long t)
{
	
	if (!_is_init)
	{
		return false;
	}
	else if (_is_busy)
	{
		return false;
	}

	Wire.beginTransmission(_address); 	         
	Wire.write(cmd);        			        
	byte wireres = Wire.endTransmission(true);  

	if (wireres == I2C_RESPONSE_OK)
	{
		//The command was sent ok, so no more comunication can be done now
		_is_busy = true;
		//At this point we where supposed to have an answer from the sensor
		//used to calculate if the result should be available
		_async_comand_ready_by = millis() + t;

		//Reset the bufer and read counter
		_clean_buffer();

#ifdef ATLAS_DEBUG
		Serial.printf("AtlasStamp::_command_async() [END] T[%d] BUSY[%d] READY_BY[%d] SUCCSED[%d] COMMAND[%s]\n", millis(), _is_busy, _async_comand_ready_by, wireres, cmd);
#endif
		//Si hemos procesado correctamente un comando y el cacharro estaba dormido se habra despertado,
		//as� que fijamos el flag, ojo, cuando lleguemos aqui despues de enviar el comando Sleep el flag
		//se pondra a true, estando realmente dormido el modulo, pero no es problema, ya que al salir y
		//volver a la funcion sleep() se pone a false
		is_awake = true;
		return true;
	}
	else
	{
		return false;
	}
}

//Obtiene el resultado de un comando asincrono
uint8_t AtlasStamp::_command_result()
{
	uint8_t tmp_char = 0;
	uint8_t _i2c_response_code = 254;

	if (!_is_init)
	{
		return ATLAS_ERROR_RESPONSE;
	}
	else if (!_is_busy)
	{
		return ATLAS_NODATA_RESPONSE;
	}
	else if (!available())
	{
		return ATLAS_BUSY_RESPONSE;
	}

	/*_clean_buffer();*/								//Limpiamos el buffer

	//Nos aseguramos de que esta listo antes de seguir :)
	while (_i2c_response_code == ATLAS_BUSY_RESPONSE) {
		Wire.requestFrom(_address, (uint8_t)MAX_DATA_TO_READ);
		_i2c_response_code = Wire.read();   
		if (_i2c_response_code == ATLAS_BUSY_RESPONSE)
		{
			//Esto es necesario si no queremos que se cuelgue el i2c
			//Tenemos que limpiar los datos del buffer antes de volver a usar el bus
			_clean_wire();
		}
		delay(CONNECTION_DELAY_MS);
	}

	//OK, ha procesado el comando y la respuesta es correcta :)
	//vamos a recuperar los datos
	if (_i2c_response_code == ATLAS_SUCCESS_RESPONSE)
	{
		//Mientras tengamos datos cargamos el buffer
		while (Wire.available())
		{
			//Obtenemos el primer byte
			tmp_char = Wire.read();

			//Si el caracter es NULL es el final de la transmision
			if (tmp_char == NULL_CHARACTER)
			{
				//A�adimos el final de carro al buffer
				_response_buffer[_i2c_bytes_received] = '\0';
				//_i2c_bytes_received++;
				//Terminamos
				break;
			}
			else
			{
				//Guardamos ese jugoso caracter en nuestro array
				_response_buffer[_i2c_bytes_received] = tmp_char;        //load this byte into our array.
				_i2c_bytes_received++;
				//TODO: Controlar aqu� el buffer overflow!
			}
		}
	}
	_clean_wire();
	//Volvemos a poner los flags en su sitio
	_is_busy = false;
	_async_comand_ready_by = 0;

#ifdef ATLAS_DEBUG
	Serial.printf("AtlasStamp::_command_result() [END] T[%d] BUSY[%d] CODE[%d] RESPONSE[%s] TIMEOUT[%d]\n", millis(), _is_busy, _i2c_response_code, _response_buffer, _async_comand_ready_by);
#endif

	//Devolvemos el codigo de respuesta :)
	//Si es 1 tendremos _response_buffer cargado con la respuesta al comando
	return _i2c_response_code;
}


uint8_t AtlasStamp::_command(char* cmd, unsigned long t)
{
#ifdef ATLAS_DEBUG
	Serial.printf("AtlasStamp::_command() [START] CMD[%s] T:[%d]\n", cmd, millis());
#endif

	if (_command_async(cmd, t))
	{
		uint8_t r = 0;
		delay(static_cast<unsigned long>(t/2.0f));
		while (ATLAS_BUSY_RESPONSE == (r = _command_result())) { delay(50); }

		if (ATLAS_SUCCESS_RESPONSE == r)
		{
			return true;
		}
	}
	return false;
}

uint8_t AtlasStamp::read_ascii(char* buffer)
{
	if (ATLAS_SUCCESS_RESPONSE == _command(ATLAS_READ_COMAND, 1000))
	{
		strcpy(buffer, _response_buffer);
		return _i2c_bytes_received;
	}
	return 0;
	
}

uint8_t AtlasStamp::result_ascii_async(char* buffer)
{
	if (ATLAS_SUCCESS_RESPONSE == _command_result())
	{
		strcpy(buffer, _response_buffer);
		return _i2c_bytes_received;
	}
	return 0;

}

float* const AtlasStamp::read()
{
	if (ATLAS_SUCCESS_RESPONSE == _command(ATLAS_READ_COMAND, 1000))
	{
		return _parse_sensor_read();
	}
	return nullptr;
}

bool const AtlasStamp::read_async()
{
	return _command_async(ATLAS_READ_COMAND, 1000);
}

float* const AtlasStamp::result_async()
{
	if (ATLAS_SUCCESS_RESPONSE == _command_result())
	{
		return _parse_sensor_read();
	}
	return nullptr;
}

float* const AtlasStamp::_parse_sensor_read(void)
{
	//Cuando llamamos a esta fucnion deberiamos tener en el _response_buffer una cadena 
	//representando la medida del sensor, esta dependera, pudiendo ser, un float (58.7)
	// o una lista de floats separada por comas (12.5,22.5,1.0,00.2)
#ifdef ATLAS_DEBUG
	Serial.printf("AtlasStamp::_parse_sensor_read() T[%lu] sensor fields [%d] response buffer [%s]\n", millis(), _response_field_count, _response_buffer);
#endif

	if (_response_field_count > 1)
	{
		char *current_token;
		current_token = strtok(_response_buffer,",");
		for (int i = 0; i < _response_field_count; i++)
		{
			if (current_token != NULL)
			{
				*(_last_result+i) = atof(current_token);
				//Get next value if previous was not null
				current_token = strtok(NULL, ",");
			}
			else
			{
				//The sensor is suposed to have multiple values, but
				//thats not true so set the value to default error
				*(_last_result + i) = -2048.0f;
			}
#ifdef ATLAS_DEBUG
			Serial.printf("Field[%d] value[%4.2f] current_token[%s]\n", i+1, *(_last_result + i), current_token);
#endif
		}
	}
	else
	{
		if (strcmp("No output", _response_buffer) == 0)
		{
			*_last_result = -2048.0f;
		}
		else
		{
			//Is a simple sensor only a float string in the buffer, just set it.
			*_last_result = atof(_response_buffer);
		}

#ifdef ATLAS_DEBUG
		Serial.printf("Field[%d] value[%4.2f]\n", 1, *_last_result);
#endif
	}
	return _last_result;
}

bool AtlasStamp::_stamp_connected()
{
	bool r = false;
	//If we are already inicialized return true
	if (_is_init) { return true; }

	//Temporary set the flag to allow the initial contact
	//otherways the _command() function wont return ATLAS_SUCCESS_RESPONSE
	_is_init = true;

	for (int i = 0; i < MAX_CONNECTION_TRIES; i++)
	{
		//Try to get the status of the device
		if (ATLAS_SUCCESS_RESPONSE == _command(ATLAS_INFO_COMAND,300))
		{
			//Is an EZO module
			if (_response_buffer[0] == '?' && _response_buffer[1] == 'I')
			{
				//comeback with good news
				r = true;
				break;
			}
		}
		delay(CONNECTION_DELAY_MS);
	}
	//Clear the flag, is the child class the one that should set it
	//when more info about the module is parsed
	_is_init = false;
	//Return r, it contains true if we could stablish communication 
	//with an EZO module in the given address
	return r;
}

void AtlasStamp::purge()
{
	//TODO: deberia esperar a que termine lo que este haciendo si
	//hay trabajo a medio?
	_is_busy = false;
	_async_comand_ready_by = 0;
}

float AtlasStamp::get_vcc(void)
{
	//float returnVal = -2048.0f;
	if (ATLAS_SUCCESS_RESPONSE == _command("Status", 200))
	{
		//?|S|T|A|T|U|S|,|P|,|5|.|0|6|4|NULL
		//TODO: Deberiamos proteger estas lecturas con punteros con algo del tipo? if (_bytes_in_buffer() >= 10) {...}
		//En este caso el atof mirara los bytes que considere y si el resultado no es correcto devolvera 0.0f asi que por ah� no parece
		//haber problemas, ademas no deberia pasar ya que si command() devuelve exito, deberia estar cargado el buffer...
		if (_bytes_in_buffer() >= 13)
		{
			char* res_buff = (char*)(_get_response_buffer() + 10);
			return atof(res_buff);
		}
	}
	return -2048.0f;
}


void AtlasStamp::info(Stream& output)
{
	output.printf("ADDRESS:[0x%02x] VERSION:[%s] READY:[%d] BUSY:[%d] MIN:[%4.3f] MAX:[%4.3f] UNIT:[%s] VCC:[%4.4f]", _address, stamp_version, _is_init, _is_busy, _min_value, _max_value, _unit, get_vcc());
}


bool AtlasStamp::led()
{
	if (ATLAS_SUCCESS_RESPONSE == _command("L,?", 150))
	{
		//Una vez qui tenemos en el bufer algo asi (las | separan, no cuentan como caracter en el buffer)
		// ? | L | , | 1 | null
		// ? | L | , | 0 | null
		//Comporbamos que la posicion 3 del buffer sea un 0 o un 1
		if (_read_buffer(3) == '1')
		{
			return true;
		}
	}
	return false;
}

bool AtlasStamp::led(bool state)
{
	//El comando de fijar el led es:
	// L,1 para activar
	// � 
	// L,0 para desactivar
	sprintf(_command_buffer,"L,%d", state);
	//Send the command and wait for confirmation
	if (ATLAS_SUCCESS_RESPONSE == _command(_command_buffer, 300))
	{
		return true;
	}
	return false;
}

bool const AtlasStamp::sleep(void)
{
	//Send the command and wait for confirmation
	if (ATLAS_SUCCESS_RESPONSE == _command("Sleep", 300))
	{
		is_awake = false;
		return true;
	}
	return false;
}

//Not necesary because if the module is sleeping will wake up with any command,
//but I use it to keep the API clean (sleep()/sleeping()/wakeup()) 
bool const AtlasStamp::wakeup(void)
{
	//If already awake return true
	if (is_awake)
	{
		return true;
	}
	return led();
}