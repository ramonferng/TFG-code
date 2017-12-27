#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <WiFiManager.h>      // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <IRremoteESP8266.h>
#include <IRDaikinESP.h>
#include <IRMitsubishiAC.h>
#include <IRKelvinator.h>
#include <FujitsuHeatpumpIR.h>



//Esta programado para que en el mensaje (Ej.: NEC 1 20DF10EF 32 3) se puedan poner tanto mayúsculas 
//como minúsculas. Y que en ese caso en concreto de ejemplo, el último número que aparece que es el 3, 
//si no se escribe, él lo toma por defecto como 0.

char mqtt_server[40];
char mqtt_port[6] = "1883";
bool shouldSaveConfig = false; //flag for saving data

String marca;
String mando;
String tecla;
String topico= "ESP";
bool inicial=true;
WiFiClient espClient;
PubSubClient client(espClient);
IRrecv irrecv(D5);
IRDaikinESP daikinir(D8);
IRMitsubishiAC mitsubir(D8);
IRKelvinatorAC kelvir(D8);
IRSenderBitBang irSender(D8); 
FujitsuHeatpumpIR *heatpumpIR =  new FujitsuHeatpumpIR(); 
IRsend irsend(D8);

void setup() {
  //Se realiza un incremento del perro guardian para evitar que este se desborde en el bucle while de parseo del mensaje RAW
  ESP.wdtEnable(2);
  pinMode(D1,OUTPUT);
  Serial.begin(9600);
  irsend.begin();
  daikinir.begin();
  mitsubir.begin();
  kelvir.begin();
  topico.concat(String(ESP.getChipId()));
  Serial.print("topico: "); 
  Serial.println(topico);

  //read configuration from FS json (MQTT server and port)
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
    
  //setup_wifi();
  WiFiManager wifiManager;
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);

  wifiManager.setSaveConfigCallback(saveConfigCallback); // Ver ejemplo en https://github.com/tzapu/WiFiManager/blob/master/examples/AutoConnectWithFSParameters/AutoConnectWithFSParameters.ino
  wifiManager.setConfigPortalTimeout(180);
  if (!wifiManager.autoConnect())
  {
    Serial.println("Timeout durint WiFi autoconnect");
    delay(3000);
    
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  IPAddress ip = WiFi.localIP();
  Serial.println("Connected to " + WiFi.SSID() + " with IP " + ip[0] + "." + ip[1] + "." + ip[2] + "." + ip[3] + "...");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  // Conectando con el servidor MQTT 
  Serial.println("SERVIDOR MQTT: ");
  Serial.println(mqtt_server);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);       // Función que se llama cuando nos llega una notificación en el topico al que estamos subscritos.
  reconnect();
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (inicial==true){
    inicial=false;
  }
  else{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();


    // SOLO PARA DEMOSTRACION DE RECIBIR ORDEN DE UN BOTÓN
    if (String(topic)==topico) {        
      int aux =-1;
      for (int i = 0; i< length && aux==-1; i++){
        if (payload[i]==32){
          aux=i;
        }
      }
      String protocolo = "";
      for (int i = 0; i< aux; i++){
        protocolo = protocolo + (char)payload[i];
      }
      char t[1];
      t[0]=payload[aux+1];
      int tipo = ((String)t).toInt();
      Serial.print("tipo ");
      Serial.println(tipo);
      aux=aux+2;

      //SOLICITUD DE DETECCIÓN DE COMANDOS RAW GENÉRICO
   
      if (tipo==9){
        marca=protocolo;
        int aux2=aux+1;
        for (int i = aux2; i< length; i++){
          if (payload[i]==32){
            aux=i;
          }
        }
        mando = "";
        for (int i = aux2; i< aux; i++){
          mando = mando + (char)payload[i];
        }
        Serial.println("mando:");
        Serial.println(mando); 
        aux2=aux+1;
        tecla = "";
        for (int i = aux2; i< length; i++){
           tecla = tecla + (char)payload[i];
        }
        Serial.println("tecla:");
        Serial.println(tecla);  
        Serial.println("RECEPTOR ACTIVADO");
        irrecv.enableIRIn();
        decode_results  results;        // Somewhere to store the results
        int fin=0;
        ESP.wdtDisable(); 
        digitalWrite(D1, HIGH); 
        while (fin==0){
          Serial.println("RECIBIENDO");
          if (irrecv.decode(&results)) {  // Grab an IR code
            String mensaje=dumpCode(&results,marca,mando,tecla);           // Output the results as source code
            Serial.println("PUBLICANDO EN TOPICO"); 
            Serial.println(mensaje);
            irrecv.disableIRIn();
            char buf[mensaje.length()];
            mensaje.toCharArray(buf,mensaje.length()+1);
            char top[topico.length()];
            topico.toCharArray(top,topico.length()+1);
            client.publish(top,buf );
            fin=1;
          }
        }
        ESP.wdtEnable(2); 
        digitalWrite(D1, LOW);  
      } 

      // EMISIÓN DE UN RAW GENÉRICO
  
      if (tipo==0){
        int maxLength=0;
        for (int i = aux; i< length; i++){
          if ((payload[i]<48 || payload[i]>57)){
            maxLength=maxLength+1;
          }
        }
        unsigned int buf[maxLength];
        int contador=0;
        int inicio=0;
        while(aux<length){
          inicio=aux;
          while (inicio<length && (payload[inicio]<48 || payload[inicio]>57)){
            inicio++;
          }
          int fin=inicio+1;
          while (fin<length && payload[fin]>=48 && payload[fin]<=57){
            fin++;
          }
          if (fin>inicio){
            char vector[fin - inicio];
            for (int i = 0; i<(fin-inicio) ; i++){
              vector[i]=payload[i+inicio];
            }
            buf[contador]=String (vector).toInt();
            contador++;
            aux=fin+1;
            memset(vector, 0, sizeof(vector));
          }   
        }
        int muestras=contador-1;
        if (protocolo=="GC"){
          muestras=muestras+1;
        }
        unsigned int raw[muestras];
        Serial.println("raw");
        for (int i = 0; i<(muestras) ; i++){
          raw[i]=buf[i];
          Serial.print(raw[i]);
          Serial.print(" ");
        }
        Serial.println();
        int freq=buf[muestras];
        Serial.println(" frecuencia");
        Serial.println(freq);
        Serial.println(" longitud");
        Serial.println(muestras);
        //unsigned int  rawData[67] = {9050,4500, 600,550, 600,550, 650,550, 600,550, 600,550, 650,550, 600,550, 600,550, 650,1700, 600,1700, 600,1700, 650,1650, 600,1650, 600,1700, 650,1650, 600,1700, 600,1700, 650,550, 600,1700, 650,1700, 600,550, 650,550, 600,1700, 650,550, 600,550, 600,1650, 600,550, 600,550, 650,1700, 600,1650, 600,550, 600,1700, 600};  // NEC FFB24D
        if (protocolo=="GC"){
          Serial.println("ENVIANDO GC");
          irsend.sendGC(raw,muestras);
        }   
        else{
          Serial.println("ENVIANDO RAW");
          irsend.sendRaw(raw,muestras,freq);
        }
        memset(raw, 0, sizeof(raw));
  
      }
      
      //SI TIPO ES 1, QUIERE DECIR QUE MANDAMOS COMANDO HEX Y DE UN DETERMINADO PROTOCOLO
   
      if (tipo==1){
        Serial.println(protocolo);
        //Ponerlos en mayúsculas siempre
        // Ahora protocolo NEC, SONY, JVC, SHERWOOD
        if (protocolo=="NEC" || protocolo=="SONY" || protocolo=="JVC" || protocolo=="SHERWOOD" ){
          int fin=aux;
          for (int i = aux; i< length && fin==aux; i++){
            if (payload[i]==32){
              fin=i;
            }
          }
          int espacio=fin;
          unsigned long codigo=0;
          int exponente=0;
          int coef;
          fin=fin-1;
          while( fin>aux && payload[fin]!= 'x' && payload[fin]!= 'X'){
            switch(payload[fin]){
              case 'a':
                coef=10;
              break;
              case 'A':
                coef=10;
              break; 
              case 'B':
                coef=11;
              break;
              case 'b':
                coef=11;
              break;
              case 'c':
                coef=12;
              break;
              case 'C':
                coef=12;
              break;
              case 'd':
                coef=13;
              break;
              case 'D':
                coef=13;
              break;
              case 'e':
                coef=14;
              break;
              case 'E':
                coef=14;
              break;
              case 'f':
                coef=15;
              break;
              case 'F':
                coef=15;
              break;
              default:
                coef=payload[fin]-48;
              break;
            }
            codigo=codigo+coef*pow(16,exponente);
            exponente++;
            fin--;
          } 
          aux=espacio+1;
          int encontrado=0;
          for (int i = aux; i< length && encontrado==0; i++){
            if (payload[i]==32){
              encontrado=1;
            }
            fin=i;
          }
          char vector[fin - aux];
          for (int i = 0; i<(fin-aux) ; i++){
            vector[i]=payload[i+aux];
          }
          int nbits=  String (vector).toInt();
          memset(vector, 0, sizeof(vector));
          //Si no hemos puesto nada en la parte de "repetir" del mensaje
          unsigned int repetir=1;
          if (protocolo=="NEC") {
            repetir=0;
          }
          //Se comprueba si en el mensaje hay algun valor en la parte de "repetir"
          fin=fin+1;
          if (encontrado==1 && fin < length ){
            char vector[length - fin];
            for (int i = 0; i<(length - fin) ; i++){
              vector[i]=payload[i+fin];
            }
            repetir=  String (vector).toInt();
            memset(vector, 0, sizeof(vector));
          }   
          Serial.print("codigo ");
          Serial.println(codigo);
          Serial.print("nbits ");
          Serial.println(nbits); 
          Serial.print("repetir ");
          Serial.println(repetir); 
          if (protocolo == "NEC"){
            Serial.println("ENVIANDO NEC");
            irsend.sendNEC(codigo,nbits,repetir); 
          }
          if (protocolo == "SONY"){
            Serial.println("ENVIANDO SONY");
            irsend.sendSony(codigo,nbits,repetir); 
          }
          if (protocolo == "SHERWOOD"){
            Serial.println("ENVIANDO SHERWOOD");
            irsend.sendSherwood(codigo,nbits,repetir); 
          }
          if (protocolo == "JVC"){
            Serial.println("ENVIANDO JVC");
            irsend.sendJVC(codigo,nbits,repetir); 
          } 
        }
        //Ponerlos en mayúsculas siempre
        // Ahora protocolo COOLIX, WHYNTER, LG, RC5, RC6, DISH, SHARP, SAMSUNG, DENON
        if (protocolo=="COOLIX" || protocolo=="WHYNTER" || protocolo=="LG" || protocolo=="RC5" ||protocolo=="RC6" ||protocolo=="DISH" || protocolo=="SHARP" || protocolo=="SAMSUNG" || protocolo=="DENON"|| protocolo=="PANASONIC" ){
          int fin=aux;
          for (int i = aux; i< length && fin==aux; i++){
            if (payload[i]==32){
              fin=i;
            }
          }
          int espacio=fin;
          unsigned long codigo=0;
          int exponente=0;
          int coef;
          fin=fin-1;
          while( fin>aux && payload[fin]!= 'x' && payload[fin]!= 'X'){
            switch(payload[fin]){
              case 'a':
                coef=10;
              break;
              case 'A':
                coef=10;
              break; 
              case 'B':
                coef=11;
              break;
              case 'b':
                coef=11;
              break;
              case 'c':
                coef=12;
              break;
              case 'C':
                coef=12;
              break;
              case 'd':
                coef=13;
              break;
              case 'D':
                coef=13;
              break;
              case 'e':
                coef=14;
              break;
              case 'E':
                coef=14;
              break;
              case 'f':
                coef=15;
              break;
              case 'F':
                coef=15;
              break;
              default:
                coef=payload[fin]-48;
              break;
            }
            codigo=codigo+coef*pow(16,exponente);
            exponente++;
            fin--;
          } 
          aux=espacio+1;
          int encontrado=0;
          for (int i = aux; i< length && encontrado==0; i++){
            if (payload[i]==32){
              encontrado=1;
            }
            fin=i;
          }
          char vector[fin - aux +1];
          for (int i = 0; i<(fin-aux+1) ; i++){
            vector[i]=payload[i+aux];
          }
          int nbits=  String (vector).toInt();
          memset(vector, 0, sizeof(vector));
          Serial.print("codigo ");
          Serial.println(codigo);
          Serial.print("nbits ");
          Serial.println(nbits); 
          if (protocolo == "COOLIX"){
            Serial.println("ENVIANDO COOLIX");
            irsend.sendCOOLIX(codigo,nbits); 
          }
          if (protocolo == "WHYNTER"){
            Serial.println("ENVIANDO WHYNTER");
            irsend.sendWhynter(codigo,nbits); 
          }
          if (protocolo == "LG"){
            Serial.println("ENVIANDO LG");
            irsend.sendLG(codigo,nbits); 
          }
          if (protocolo == "RC5"){
            Serial.println("ENVIANDO RC5");
            irsend.sendRC5(codigo,nbits); 
          }
          if (protocolo == "RC6"){
            Serial.println("ENVIANDO RC6");
            irsend.sendRC6(codigo,nbits); 
          }        
          if (protocolo == "DISH"){
            Serial.println("ENVIANDO DISH");
            irsend.sendDISH(codigo,nbits); 
          }      
          if (protocolo == "SHARP"){
            Serial.println("ENVIANDO SHARP TIPO 1");
            irsend.sendSharpRaw(codigo,nbits); 
          }      
          if (protocolo == "SAMSUNG"){
            Serial.println("ENVIANDO SAMSUNG");
            irsend.sendSAMSUNG(codigo,nbits); 
          }
          if (protocolo == "DENON"){
            Serial.println("ENVIANDO DENON");
            irsend.sendDenon(codigo,nbits); 
          }     
          if (protocolo == "PANASONIC"){
            Serial.println("ENVIANDO PANASONIC");
            irsend.sendPanasonic(nbits,codigo); 
          }    
        }
      } 
      if (tipo==2) {
        int aux2=aux+1;
        for (int i = aux2; i< length; i++){
          if (payload[i]==32){
            aux=i;
          }
        }
        String direccion = "";
        for (int i = aux2; i< aux; i++){
          direccion = direccion + (char)payload[i];
        }
        Serial.println("direccion:");
        Serial.println(direccion); 
        aux2=aux+1;
        String comando = "";
        for (int i = aux2; i< length; i++){
          comando = comando + (char)payload[i];
        }
        Serial.println("comando:");
        Serial.println(comando); 
        if (protocolo=="SHARP"){
          Serial.println("ENVIANDO SHARP TIPO 2");
          irsend.sendSharp(direccion.toInt(),comando.toInt()); 
        }      
      }
      if (tipo==3){
        aux=aux+1;
        unsigned char data[length-aux];
        for (int i = 0; i< length-aux; i++){
          data[i]=payload[i+aux];
          Serial.print(data[i]);
        }
        Serial.println();
        if (protocolo=="DAIKIN"){
          Serial.println("ENVIANDO DAIKIN");
          irsend.sendDaikin(data);
        }
        if (protocolo=="KELVINATOR"){
          Serial.println("ENVIANDO KELVINATOR");
          irsend.sendKelvinator(data); 
        }
        if (protocolo=="MITSUBISHI"){
          Serial.println("ENVIANDO MITSUBISHI");
          irsend.sendMitsubishiAC(data);
        }
      }
      if (tipo==4){
        t[0]=payload[aux+1];
        int orden = ((String)t).toInt();
        aux=aux+2;
        String v = "";
        for (int i = aux; i< length; i++){
          v = v + (char)payload[i];
        }
        int valor = v.toInt(); 
        if(protocolo=="DAIKIN"){
          switch(orden){
              case 0:
                if(valor==0){
                  daikinir.off();
                  }
                else{
                  daikinir.on();  
                }
              break;
              case 1:
                if(valor==0){
                  daikinir.setAux(DAIKIN_POWERFUL);
                  }
                else{
                  daikinir.setAux(DAIKIN_SILENT);  
                }
              break;
              case 2:
                daikinir.setTemp(valor);
              break;
              case 3:
                daikinir.setFan(valor);
              break;
              case 4:
                switch(valor){
                  case 0:
                    daikinir.setMode(DAIKIN_COOL);
                  break;
                  case 1:
                    daikinir.setMode(DAIKIN_HEAT);
                  break;
                  case 2:
                    daikinir.setMode(DAIKIN_FAN);
                  break;
                  case 3:
                    daikinir.setMode(DAIKIN_AUTO);
                  break;
                  case 4:
                    daikinir.setMode(DAIKIN_DRY);
                  break;
                }
              break;
              case 5:
                daikinir.setSwingVertical(valor);
              break;
              case 6:
                daikinir.setSwingHorizontal(valor);
              break;
              default:
                Serial.println("Orden incorrecta");
              break;
            }
            daikinir.send(); 
            String estado=protocolo + " 5 ";
            if(daikinir.getPower()==0){
              estado=estado+ "POWER: OFF Aux: ";
            } 
            else{
              estado=estado +"POWER: ON Aux: ";
            }
            if(daikinir.getAux()==2){
              estado=estado+ "POWERFUL ";
            } 
            else{
              estado=estado +"SILENT ";
            }
            estado=estado + "Temp: " + String(daikinir.getTemp()) + " Fan: ";
            if(daikinir.getFan()==0){
              estado=estado+ "AUTO Mode: ";
            }
            else{
              estado=estado + String(daikinir.getFan()) + " Mode: ";
            }
            switch(daikinir.getMode()){
                  case 3:
                    estado= estado + "COOL ";
                  break;
                  case 4:
                    estado= estado + "HEAT ";
                  break;
                  case 6:
                    estado= estado + "FAN ";
                  break;
                  case 0:
                    estado= estado + "AUTO ";
                  break;
                  case 2:
                    estado= estado + "DRY ";
                  break;
            } 
            estado= estado + "Swing Vertical: ";
            if(daikinir.getSwingVertical()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }         
            estado= estado + "Swing Horizontal: ";  
            if(daikinir.getSwingHorizontal()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }               
            char bufEstado[estado.length()];
            estado.toCharArray(bufEstado,estado.length()+1);
            char top[topico.length()];
            topico.toCharArray(top,topico.length()+1);
            Serial.println("Publicando Estado");
            Serial.println(estado);
            client.publish(top,bufEstado);
        }
        if(protocolo=="MITSUBISHI"){
          switch(orden){
              case 0:
                if(valor==0){
                  mitsubir.off();
                  }
                else{
                  mitsubir.on();  
                }
              break;
              case 1:
                if(valor==0){
                  mitsubir.setVane(MITSUBISHI_AC_VANE_AUTO);
                  }
                else{
                  mitsubir.setVane(MITSUBISHI_AC_VANE_AUTO_MOVE);  
                }
              break;
              case 2:
                mitsubir.setTemp(valor);
              break;
              case 3:
                if(valor==0){
                  mitsubir.setFan(MITSUBISHI_AC_FAN_AUTO);
                }
                if(valor==1){
                  mitsubir.setFan(MITSUBISHI_AC_FAN_MAX);
                }
                if(valor==2){
                  mitsubir.setFan(MITSUBISHI_AC_FAN_SILENT);
                }
              break;
              case 4:
                switch(valor){
                  case 0:
                    mitsubir.setMode(MITSUBISHI_AC_AUTO);
                  break;
                  case 1:
                    mitsubir.setMode(MITSUBISHI_AC_COOL);
                  break;
                  case 2:
                    mitsubir.setMode(MITSUBISHI_AC_DRY);
                  break;
                  case 3:
                    mitsubir.setMode(MITSUBISHI_AC_HEAT);
                  break;
                }
              break;
              default:
                Serial.println("Orden incorrecta");
              break;
            }
            mitsubir.send(); 
            String estado=protocolo + " 5 ";
            if(mitsubir.getPower()==0){
              estado=estado+ "POWER: OFF Vane: ";
            } 
            else{
              estado=estado +"POWER: ON Vane: ";
            }
            if(mitsubir.getVane()==7){
              estado=estado+ "AUTO ";
            } 
            else{
              estado=estado +"AUTO_MOVE ";
            }
            estado=estado + "Temp: " + String(mitsubir.getTemp()) + " Fan: ";
            if(mitsubir.getFan()==0){
              estado=estado+ "AUTO Mode: ";
            }
            else{
              estado=estado+ "SILENT Mode: ";
            }
            switch(mitsubir.getMode()){
                  case 32:
                    estado= estado + "AUTO ";
                  break;
                  case 24:
                    estado= estado + "COOL ";
                  break;
                  case 16:
                    estado= estado + "DRY ";
                  break;
                  case 8:
                    estado= estado + "HEAT ";
                  break;
            }          
            char bufEstado[estado.length()];
            estado.toCharArray(bufEstado,estado.length()+1);
            char top[topico.length()];
            topico.toCharArray(top,topico.length()+1);
            Serial.println("Publicando Estado");
            Serial.println(estado);
            client.publish(top,bufEstado);
        } 

        if(protocolo=="KELVINATOR"){
          switch(orden){
              case 0:
                if(valor==0){
                  kelvir.off();
                  }
                else{
                  kelvir.on();  
                }
              break;
              case 1:
                if(valor==0){
                  kelvir.setQuiet(false);
                  }
                else{
                  kelvir.setQuiet(true);  
                }
              break;
              case 2:
                kelvir.setTemp(valor);
              break;
              case 3:
                kelvir.setFan(valor);
              break;
              case 4:
                switch(valor){
                  case 0:
                    kelvir.setMode(KELVINATOR_AUTO);
                  break;
                  case 1:
                    kelvir.setMode(KELVINATOR_COOL);
                  break;
                  case 2:
                    kelvir.setMode(KELVINATOR_DRY);
                  break;
                  case 3:
                    kelvir.setMode(KELVINATOR_FAN);
                  break;
                  case 4:
                    kelvir.setMode(KELVINATOR_HEAT);
                  break;
                }
              break;
              case 5:
                kelvir.setSwingVertical(valor);
              break;
              case 6:
                kelvir.setSwingHorizontal(valor);
              break;
              case 7:
                kelvir.setIonFilter(valor);
              break;
              case 8:
                kelvir.setLight(valor);
              break;
              case 9:
                kelvir.setTurbo(valor);
              break;
              default:
                Serial.println("Orden incorrecta");
              break;
            }
            kelvir.send(); 
            String estado=protocolo + " 5 ";
            if(kelvir.getPower()==0){
              estado=estado+ "POWER: OFF Quiet: ";
            } 
            else{
              estado=estado +"POWER: ON Quiet: ";
            }
            if(kelvir.getQuiet()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }
            estado=estado + "Temp: " + String(kelvir.getTemp()) + " Fan: ";
            if(kelvir.getFan()==0){
              estado=estado+ "AUTO Mode: ";
            }
            else{
              estado=estado + String(kelvir.getFan()) + " Mode: ";
            }
            switch(kelvir.getMode()){
                  case 0:
                    estado= estado + "AUTO ";
                  break;
                  case 1:
                    estado= estado + "COOL ";
                  break;
                  case 2:
                    estado= estado + "DRY ";
                  break;
                  case 3:
                    estado= estado + "FAN ";
                  break;
                  case 41:
                    estado= estado + "HEAT ";
                  break;
            } 
            estado= estado + "Swing Vertical: ";
            if(kelvir.getSwingVertical()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }         
            estado= estado + "Swing Horizontal: ";
            if(kelvir.getSwingHorizontal()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }         
            estado= estado + "Ion Filter: ";
            if(kelvir.getIonFilter()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }         
            estado= estado + "Light: ";
            if(kelvir.getLight()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }         
            estado= estado + "Turbo: ";
            if(kelvir.getTurbo()==false){
              estado=estado+ "OFF ";
            } 
            else{
              estado=estado +"ON ";
            }            
            char bufEstado[estado.length()];
            estado.toCharArray(bufEstado,estado.length()+1);
            char top[topico.length()];
            topico.toCharArray(top,topico.length()+1);
            Serial.println("Publicando Estado");
            Serial.println(estado);
            client.publish(top,bufEstado);
        }
     
      } 
      if (tipo==6){
         if(protocolo=="FUJITSU"){
            int aux2=aux+1;
            bool espacio= false;
            for (int i = aux2; i< length && espacio==false ; i++){
              if (payload[i]==32){
                aux=i;
                espacio=true;
              }
            }
            String power = "";
            for (int i = aux2; i< aux; i++){
              power = power + (char)payload[i];
            }
            aux2=aux+1;
            espacio= false;
            for (int i = aux2; i< length && espacio==false ; i++){
              if (payload[i]==32){
                aux=i;
                espacio=true;
              }
            }
            String modo = "";
            for (int i = aux2; i< aux; i++){
              modo = modo + (char)payload[i];
            }
            aux2=aux+1;
            espacio= false;
            for (int i = aux2; i< length && espacio==false ; i++){
              if (payload[i]==32){
                aux=i;
                espacio=true;
              }
            }
            String fan = "";
            for (int i = aux2; i< aux; i++){
              fan = fan + (char)payload[i];
            }
            aux2=aux+1;
            espacio= false;
            for (int i = aux2; i< length && espacio==false ; i++){
              if (payload[i]==32){
                aux=i;
                espacio=true;
              }
            }
            String temp = "";
            for (int i = aux2; i< aux; i++){
              temp = temp + (char)payload[i];
            }
            aux2=aux+1;
            espacio= false;
            for (int i = aux2; i< length && espacio==false ; i++){
              if (payload[i]==32){
                aux=i;
                espacio=true;
              }
            }
            String swingv = "";
            for (int i = aux2; i< aux; i++){
              swingv = swingv + (char)payload[i];
            }
            aux2=aux+1;
            espacio= false;
            for (int i = aux2; i< length && espacio==false ; i++){
              if (payload[i]==32){
                aux=i;
                espacio=true;
              }
            }
            String swingh = "";
            for (int i = aux2; i< aux; i++){
              swingh = swingh + (char)payload[i];
            }            
            aux2=aux+1;
  
            String eco = "";
            for (int i = aux2; i< length; i++){
              eco = eco + (char)payload[i];
            }
            int num_power = 1;
            if (power=="OFF"){
              num_power=0;
            }
            int num_modo = FUJITSU_AIRCON1_MODE_AUTO;
            if (modo=="HEAT"){
              num_modo=FUJITSU_AIRCON1_MODE_HEAT;
            }
             if (modo=="COOL"){
              num_modo=FUJITSU_AIRCON1_MODE_COOL;
            }
             if (modo=="DRY"){
              num_modo=FUJITSU_AIRCON1_MODE_DRY;
            }
             if (modo=="FAN"){
              num_modo=FUJITSU_AIRCON1_MODE_FAN;
            }
            int num_fan=FUJITSU_AIRCON1_FAN_AUTO;
            if (fan=="FAN1"){
              num_fan=FUJITSU_AIRCON1_FAN1;
            }
             if (fan=="FAN2"){
              num_fan=FUJITSU_AIRCON1_FAN2;
            }
             if (fan=="FAN3"){
              num_fan=FUJITSU_AIRCON1_FAN3;
            }
             if (fan=="FAN4"){
              num_fan=FUJITSU_AIRCON1_FAN4;
            }
            int num_swingv = FUJITSU_AIRCON1_VDIR_MANUAL;
             if (swingv=="SWING"){
              num_swingv=FUJITSU_AIRCON1_VDIR_SWING;
            }
            int num_swingh = FUJITSU_AIRCON1_HDIR_MANUAL;
             if (swingh=="SWING"){
              num_swingh=FUJITSU_AIRCON1_HDIR_SWING;
             }

            int num_eco = FUJITSU_AIRCON1_ECO_OFF;
             if (eco=="ON"){
              num_eco=FUJITSU_AIRCON1_ECO_ON;
            }
    
            heatpumpIR->send(irSender, num_power, num_modo, num_fan, temp.toInt(), num_swingv, num_swingh, num_eco);            
            String estado=protocolo + " 5 ";
            estado= estado + "POWER: " + power;
            estado= estado + " MODO: " + modo;
            estado= estado + " FAN: " + fan;
            estado= estado + " TEMP: " + temp;
            estado= estado + " SWING VERT: " + swingv;
            estado= estado + " SWING HOR: " + swingh;
            estado= estado + " ECO: " + eco;
            char bufEstado[estado.length()];
            estado.toCharArray(bufEstado,estado.length()+1);
            char top[topico.length()];
            topico.toCharArray(top,topico.length()+1);
            Serial.println("Publicando Estado");
            Serial.println(estado);
            client.publish(top,bufEstado);
 
         }
        
        }
          
      Serial.println();
    }
  }
}
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");      // Once connected, publish an announcement...
      client.publish("ESP8266/connection status", "Connected!"); //Primer parametro, es el topico, y el segundo es el mensaje que se veria en el movil en la subscripcion
      // ... and resubscribe
      client.subscribe("LED Ramon");
      client.subscribe("SharpRRMCGA022SJSA");
      char top[topico.length()];
      topico.toCharArray(top,topico.length()+1);
      client.subscribe(top);
    } 
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
String  dumpCode (decode_results *results, String marca, String mando, String tecla ){
  // Start declaration
  String mensaje=marca + " 8 " + mando + " " + tecla + " [";
  // Dump data
  for (int i = 1;  i < results->rawlen;  i++) {
    mensaje = mensaje + String(results->rawbuf[i] * USECPERTICK);
    if ( i < results->rawlen-1 ) mensaje= mensaje + ","; // ',' not needed on last one
    if (!(i & 1))  mensaje= mensaje + " ";
  }
  mensaje = mensaje + "] ";
  //el comando hex ira en formato int, de manera que la BD seria la encargada de convertirlo de int a hex
  if (results->decode_type == PANASONIC) {
    mensaje = mensaje + String(results->panasonicAddress);
    mensaje= mensaje + ":";
  }
  // Print Code
  mensaje = mensaje + String(results->value);
  mensaje= mensaje + " ";
  switch (results->decode_type) {
    default:
    case UNKNOWN:      mensaje = mensaje + "UNKNOWN";      break ;
    case NEC:          mensaje = mensaje +"NEC";           break ;
    case SONY:         mensaje = mensaje +"SONY";          break ;
    case RC5:          mensaje = mensaje +"RC5";           break ;
    case RC6:          mensaje = mensaje +"RC6";           break ;
    case DISH:         mensaje = mensaje +"DISH";          break ;
    case SHARP:        mensaje = mensaje +"SHARP";         break ;
    case JVC:          mensaje = mensaje +"JVC";           break ;
    case SANYO:        mensaje = mensaje +"SANYO";         break ;
    case MITSUBISHI:   mensaje = mensaje +"MITSUBISHI";    break ;
    case SAMSUNG:      mensaje = mensaje +"SAMSUNG";       break ;
    case LG:           mensaje = mensaje +"LG";            break ;
    case WHYNTER:      mensaje = mensaje +"WHYNTER";       break ;
    case AIWA_RC_T501: mensaje = mensaje +"AIWA_RC_T501";  break ;
    case PANASONIC:    mensaje = mensaje +"PANASONIC";     break ;
  }
  return mensaje;
}
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
