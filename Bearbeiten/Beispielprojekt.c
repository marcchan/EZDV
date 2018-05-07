/* Beispiel Modbus-Kommunikation, Simulation einer Switchbox
 *
 * Lesen der Eingaenge eines Modbus-Nodes und Ausgabe auf den
 * Ausgaengen der Modbus-GUI. Lesen der Eingaenge der Modbus-GUI
 * und Setzen der relevanten Ausgaenge des Modbus-Nodes. Der Transfer
 * erfolgt 16-bittig,  also fuer die Bits 0..15!
 * Ein Wert > 0 in den Bits 16..31 der Eingaenge der Modbus-GUI bricht ab.
 *
 * MODBUS_GUI anpassen (bs-pcN)!
 *
 * (c) mw, v.0.91, 10.11.09
 *
 */

#include <rtai_mbx.h>
#include <rtai_sched.h>
#include <sys/rtai_modbus.h>

MODULE_LICENSE("GPL");
 // Computer-Client, an dem gerade gearbeitet wird
#define MODBUS_GUI "bs-pc1"

// Hier sind die Sensoren der Bearbeitenstation aufgeführt
#define IN_WERKSTUECK_IM_DREHTELLER 		1<<0
#define IN_WERSTUEK_IN_BOHRVORRICHTUNG		1<<1
#define IN_WERSTUEK_IN_MESSVORRICHTUNG		1<<2
#define IN_BOHRER_OBEN						1<<3
#define IN_BOHRER_UNTEN						1<<4
#define IN_DREHTELLER_IN_POSITION			1<<5
#define IN_PRUEFER_AUSSCHUSS_ERKANNT		1<<6

// Hier sind die Aktoren der Bearbeitenstation aufgeführt
#define OUT_BOHRER							1<<0
#define OUT_DREHTELLER						1<<1
#define OUT_BOHRER_RUNTERFAHREN				1<<2
#define OUT_BOHRER_HOCHFAHREN				1<<3
#define OUT_WERSTUECK_FESTHALTEN			1<<4
#define OUT_PRUEFER_AUSFAHREN				1<<5
#define OUT_AUSWERFER_OUTPUT				1<<6
#define OUT_AUSWERFER_INPUT					1<<7

// Allgemeine Definitionen
#define JA 									1
#define NEIN 								0
#define SET 								1
#define RESET 								0
#define AUSCHUSS 									1

// MailBox Nachrichten (Inhalt)
#define MB_AUSWERFER						10
#define MB_PRUEFER							11
#define MB_BOHRER							12
#define MB_DREHTELLER						13

// Modbus-Knoten
static int fd_node;

// Semaphore-Deklarierung
static SEM sem;	//für sicheres Schreiben auf die Ausgänge

// Tasks-Deklarierung
static RT_TASK taskControl;
static RT_TASK taskAuswerfer;
static RT_TASK taskPruefer;
static RT_TASK taskBohrmaschine;
static RT_TASK taskDrehteller;

// Mailboxen-Deklarierung
enum mbx {
	mailBoxAuswerfer,
	mailBoxControl,
	mailBoxPruefer,
	mailBoxBohrmaschine,
	mailBoxDrehteller,
	lastMailBox
};
static MBX mbox[5];

// Funktions-Deklarationen
static void auswerfer(long);
static void pruefer(long);
static void bohrmaschine(long);
static void drehteller(long);
static int init_Aktoren(int);
static int writeOnModBus(uint8_t mask, uint8_t mode);

/* Hier beginnt der Control-Task */
static void control(long x) {
	// Mailgrößen für die Mailboxen
	uint8_t letter_Auswerfer = 0;
	uint8_t letter_Pruefer = 0;
	uint8_t letter_Bohrer = 0;
	uint8_t letter_Drehteller = 0;
	uint8_t letter_Control = 0;

  // lokale Variable zum Löschen der Mailboxen im Fehlerfall
	int cnt_Mail_delete;

  // Dient zur Synchronisation der Tasks: Prüfer, Bohrer und Auswerfer
  // Damit werden die Ausschussteile von den i.O. Teilen unterschieden
	uint8_t message_Counter;
	uint8_t counter_var;

	// lokale Variale für den Control-Task
	uint8_t zuletztGebohrt;
	uint8_t soll_gebohrt_werden = 0;
	static int val = 0;

	rt_printk("control: Task started\n");

	// Verbinde zu Modbusknoten
	if ((fd_node = rt_modbus_connect("MODBUS-NODE")) == -1) {
		rt_printk("control: cannot connect to modbus-node\n");
		rt_printk("control: task exited\n");
		goto fail;
	}

	rt_printk("control: MODBUS communication opened\n");

	//Alle Task werden resumed
	rt_task_resume(&taskAuswerfer);
	rt_task_resume(&taskPruefer);
	rt_task_resume(&taskBohrmaschine);
	rt_task_resume(&taskDrehteller);

  // In der Initialisierung wird die Bohrmachine zuerst hochfahren;
  // Nachnach wird der Dreheller komplett leerfahren;
	if(init_Aktoren(fd_node) == -1)
		goto fail;

	while (1) {

		/**
		 * In der while-Schleife werden die einzelnen Funktionen zu den Tasks in Abhängigkeit
		 * zu den Sensorwerten aufgerufen.
		 * Die Tasks taskAuswerfer, taskPruefer, taskBohrer werden gesondert behandelt.
		 * Diese werden nach ihrem jeweilgen Nachrichten-Inhalt hin untersucht.
		 * So muss z.B. sichergestellt werden, dass eine Ausschussteil richtig erkannt wurde.
		 * Dies wird durch die untenstehende for-Schleife erreicht.
		**/

    // Initialiserung der lokalen Varaiblen
		zuletztGebohrt = NEIN;
		message_Counter = 0;

		/* Einlesen der Eingänge*/
		if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
			goto fail;

    // Wenn jetzt ein Werkstück in der Bohrvorrichtung liegt, muss der Auswerfer nach einem erneuten Drehvorgang
    // aktiviert werden.
		if ((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG) == IN_WERSTUEK_IN_BOHRVORRICHTUNG) {
			zuletztGebohrt = JA;
			rt_printk("Werkstueck in Bohrvorrichtung\n");
		}

    // Hier wird untersucht, ob der Drehteller drehen muss. Dabei werden alle Sensoren, die ein Werkstück erkennen abgefragt.
		if (((val & IN_WERKSTUECK_IM_DREHTELLER) == IN_WERKSTUECK_IM_DREHTELLER) | ((val & IN_WERSTUEK_IN_MESSVORRICHTUNG) == IN_WERSTUEK_IN_MESSVORRICHTUNG)
				| ((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG) == IN_WERSTUEK_IN_BOHRVORRICHTUNG)) {
			rt_mbx_send(&mbox[mailBoxDrehteller], &letter_Drehteller, sizeof(letter_Drehteller));
			rt_printk("Starte Drehteller\n");
			rt_mbx_receive(&mbox[mailBoxControl], &letter_Drehteller, sizeof(letter_Drehteller)); // Startet erst, wenn Mail im Postfach vorhanden
			rt_printk("taskTwo received message : %d\n",letter_Drehteller); // Testausgabe
			if (letter_Drehteller == MB_DREHTELLER) {
				rt_printk("Drehteller steht wieder\n");
				}
		}

		if (zuletztGebohrt == JA) {
			//Auswerfer besitzt keine Sensor und benutzt den Sensor der Bohrvorrichtung
			rt_mbx_send(&mbox[mailBoxAuswerfer], &letter_Auswerfer, sizeof(letter_Auswerfer));		//Auswerfer für Test ausschalten!!!!!!!!!!!!!!!
			message_Counter++;
			rt_printk("Starte Auswerfvorgang\n");
		}

		//erneutes einlesen der Eingänge nach dem drehen des Drehtellers
		if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
			goto fail;

    // Liegt ein Werkstueck unter der Prüfvorrichtung?
		if (((val & IN_WERSTUEK_IN_MESSVORRICHTUNG)	== IN_WERSTUEK_IN_MESSVORRICHTUNG)) {
			rt_mbx_send(&mbox[mailBoxPruefer], &letter_Pruefer, sizeof(letter_Pruefer));	//starte Messvorgang
			message_Counter++;
			rt_printk("Starte Pruefvorgang\n");
		}

    // Liegt ein Werkstueck in der Bohrvorrichtung?
    // Überprüfe zusätzlich, ob auch gebohrt werden soll. Ausschuss?
		if (((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG) == IN_WERSTUEK_IN_BOHRVORRICHTUNG) && soll_gebohrt_werden == JA) {
			rt_mbx_send(&mbox[mailBoxBohrmaschine], &letter_Bohrer, sizeof(letter_Bohrer));	//starte Bohrvorgang
			message_Counter++;
			rt_printk("Starte Bohrvorgang\n");
		} else if (((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG) == IN_WERSTUEK_IN_BOHRVORRICHTUNG) && soll_gebohrt_werden == NEIN) {
			rt_printk("Werkstueck ist ein Ausschussteil 1\n");
		}

    // Diese for-Schleife synchronisiert die antwortenden Mailboxen
		for(counter_var = 0; counter_var < message_Counter; message_Counter--){
			rt_mbx_receive(&mbox[mailBoxControl], &letter_Control, sizeof(letter_Control)); //Warte bis Auswerfvorgang beendet wurde

			// Warte nur auf Mail, wenn der Auswerfer auch gestartet wurde
			if (zuletztGebohrt == JA && letter_Control == MB_AUSWERFER) {
				rt_printk("taskTwo received message : %d\n",letter_Auswerfer);
				if (letter_Auswerfer == MB_AUSWERFER) {
					rt_printk("Auswerfvorgang gestoppt\n");
					}
			}

			// Warte nur auf Mail, wenn kein Ausschussteil vorhanden ist
			if ((((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG) == IN_WERSTUEK_IN_BOHRVORRICHTUNG && soll_gebohrt_werden == JA) && letter_Control == MB_BOHRER)) {
				rt_printk("taskTwo received message : %d\n",letter_Bohrer);
					rt_printk("Bohrvorgang gestoppt\n");
			} else if (((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG) == IN_WERSTUEK_IN_BOHRVORRICHTUNG) && soll_gebohrt_werden == NEIN && letter_Control == MB_BOHRER) {
				rt_printk("Werkstueck ist ein Ausschussteil 2\n");
			}

      // Wenn Werkstueck in Messvorrichtung und die Mailinhalt ist MB_PRUEFER oder AUSSCHUSS, lege fest, ob im nächsten
      // Durchlauf gebohrt werden soll oder nicht.
			if (((val & IN_WERSTUEK_IN_MESSVORRICHTUNG) == IN_WERSTUEK_IN_MESSVORRICHTUNG) && (letter_Control == MB_PRUEFER || letter_Control == AUSCHUSS)) {
				rt_printk("taskTwo received message Ausschuss??: %d\n",letter_Control);
				if (letter_Control == MB_PRUEFER) {
					rt_printk("Pruefvorgang gestoppt\n");
					}
				if( letter_Control == AUSCHUSS){
					rt_printk("Pruefvorgang gestoppt und Ausschuss erkannt\n");
					soll_gebohrt_werden = NEIN;
				}else{
					soll_gebohrt_werden = JA;
				}
		  }
		}
	} //Ende while()

  // Sprungstelle, falls Fehler auftreten
  // Schieße Modbus-Verbindung
	fail: rt_modbus_disconnect(fd_node);
	rt_printk("control: MODBUS communication failed\n");
	rt_printk("control: task exited\n");

  // Lösche Tasks
	rt_task_delete(&taskDrehteller);
	rt_task_delete(&taskAuswerfer);
	rt_task_delete(&taskPruefer);
	rt_task_delete(&taskBohrmaschine);

  // Lösche Mailboxen
	for (cnt_Mail_delete = mailBoxAuswerfer; cnt_Mail_delete < lastMailBox; cnt_Mail_delete++)
		rt_mbx_delete(&mbox[cnt_Mail_delete]);

  // Lösche Semaphore
	rt_sem_delete(&sem);

	rt_printk("rtai_example unloaded\n");
	rt_printk("Sie muessen das Programm neu starten.\n");
}

/**
 * Hier wird das Programm ordnungsgemäß beendet.
 * */
static void __exit example_exit(void) {
  //lokale Variable zum Löschen der Mailboxen
	int i;
  // Löschen aller Tasks
	rt_task_delete(&taskDrehteller);
	rt_task_delete(&taskControl);
	rt_task_delete(&taskAuswerfer);
	rt_task_delete(&taskPruefer);
	rt_task_delete(&taskBohrmaschine);

  // Löschen aller Mailboxen
	for (i = mailBoxAuswerfer; i < lastMailBox; i++)
		rt_mbx_delete(&mbox[i]);
  // Löschen der Semaphore
	rt_sem_delete(&sem);
  // Stoppe RT_Timer
	stop_rt_timer();

	rt_printk("rtai_example unloaded\n");
	rt_printk("Sie muessen das Programm neu starten.\n");
}

static int __init example_init(void) {
	int i;

	rt_set_oneshot_mode();
	start_rt_timer(0);
	rt_typed_sem_init(&sem, 1 , CNT_SEM);
	modbus_init();

	/**
	 * Initialisieren der Mailboxen
	 * */

	for (i = mailBoxAuswerfer; i < lastMailBox; i++)
		if (rt_mbx_init(&mbox[i], sizeof(int))) {
			printk("Cannot initialize mailbox %d\n", i);
			goto fail0;
		}

	/* rt_task_init(RT_TASK *task, void (*rt_thread)(long), long data,
	 * 				int stack_size, int priority, int uses_fpu,
	 * 				void (*signal)(void))
	 */
	if (rt_task_init(&taskControl, control, 0, 10240, 0, 0, NULL)) {
		printk("cannot initialize control task\n");
		goto fail0;
	}

	if (rt_task_init(&taskAuswerfer, auswerfer, 0, 10240, 0, 0, NULL)) {
		printk("cannot initialize control task\n");
		goto fail1;
	}

	if (rt_task_init(&taskPruefer, pruefer, 0, 10240, 0, 0, NULL)) {
		printk("cannot initialize control task\n");
		goto fail2;
	}

	if (rt_task_init(&taskBohrmaschine, bohrmaschine, 0, 10240, 0, 0, NULL)) {
		printk("cannot initialize control task\n");
		goto fail3;
	}

	if (rt_task_init(&taskDrehteller, drehteller, 0, 10240, 0, 0, NULL)) {
		printk("cannot initialize control task\n");
		goto fail4;
	}

	rt_task_resume(&taskControl);

	rt_printk("rtai_example loaded\n");
	return (0);

	/**
	 * Neue Tasks müssen die Alten in umgekehrter Reihenfolge löschen.
	 *
	 * */
	fail4: rt_task_delete(&taskBohrmaschine);
	fail3: rt_task_delete(&taskPruefer);

	fail2: rt_task_delete(&taskAuswerfer);

	fail1: rt_task_delete(&taskControl);

	fail0: stop_rt_timer();
	while (i-- > 0)
		rt_mbx_delete(&mbox[i]);
	rt_sem_delete(&sem);

	rt_printk("rtai_example unloaded\n");
	rt_printk("Sie muessen das Programm neu starten.\n");
	return (1);
}

/**
 * In den untenstehenden Funktionen werden die Sensoren abgefragt und die Aktoren angesteuert.
 * */

// Der Auswerfer wird nur aktiviert, wenn auch ein Bauteil vorhanden ist. Die Auswertung findet
// im Control-Task statt.
static void auswerfer(long x) {
  // lokale Variablen: Für Mailboxgröße und das Löschen der Mailboxen im Fehlerfall
	uint8_t letter_Auswerfer;
	int cnt_Mail_Delete;

	while (1) {
		rt_mbx_receive(&mbox[mailBoxAuswerfer], &letter_Auswerfer, sizeof(letter_Auswerfer));		//Empfange Nachricht

    // Aktiviere Auswerfer
		if (writeOnModBus(OUT_AUSWERFER_OUTPUT, SET) == -1)
			goto fail;

		rt_sleep(400 * nano2count(1000000)); // Damit auch die schweren Teile ausgelagert werden

		if (writeOnModBus(OUT_AUSWERFER_OUTPUT, RESET) == -1)
			goto fail;

		letter_Auswerfer = MB_AUSWERFER;
		rt_mbx_send(&mbox[mailBoxControl], &letter_Auswerfer, sizeof(letter_Auswerfer));		//Bin fertig!
	}
  // Wenn Fehler auftreten
	fail: rt_printk("auswerfer: Modus Fehler\n");
	rt_modbus_disconnect(fd_node);
	rt_printk("auswerfer: MODBUS communication failed\n");
	rt_printk("auswerfer: task exited\n");

	rt_task_delete(&taskDrehteller);
	rt_task_delete(&taskPruefer);
	rt_task_delete(&taskBohrmaschine);
	rt_task_delete(&taskControl);

	for (cnt_Mail_Delete = mailBoxAuswerfer; cnt_Mail_Delete < lastMailBox; cnt_Mail_Delete++)
		rt_mbx_delete(&mbox[cnt_Mail_Delete]);

	rt_sem_delete(&sem);

	rt_printk("rtai_example unloaded\n");
	rt_printk("Sie muessen das Programm neu starten.\n");
}

// Der Prüfer erkennt Ausschussteile und teilt dies über einen entsprechenden Mailinhalt dem
// Control-Task mit.
static void pruefer(long x) {
  // lokale Variablen für Sensorwerte, Mailboxen löschen und Mailboxgröße
	static int val = 0;
	int cnt_Mail_Delete;
	uint8_t letter_Pruefer;

	while (1) {
		uint8_t countSleepAusschuss = 0;
		uint8_t ausschuss_erkannt = 0;
		rt_mbx_receive(&mbox[mailBoxPruefer], &letter_Pruefer, sizeof(letter_Pruefer));		//Empfange Nachricht

		if (writeOnModBus(OUT_PRUEFER_AUSFAHREN, SET) == -1)	//Prüfer herunterfahren
			goto fail;

    // In dieser Schleife wird durch das hochzählen einer Variablen Ausschuss erkannt.
    // Wird ein n.i.O. Teil erkannt, wird die entsprechende Variable "ausschuss_erkannt" gesetzt.
		do {
			countSleepAusschuss++;
			rt_sleep(50 * nano2count(1000000));

			if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
				goto fail;
			rt_printk("Noch in Schleife -> Pruefer\n");

			if ((val & IN_PRUEFER_AUSSCHUSS_ERKANNT)== IN_PRUEFER_AUSSCHUSS_ERKANNT) {
				ausschuss_erkannt = NEIN;
			} else if (((val & IN_PRUEFER_AUSSCHUSS_ERKANNT) != IN_PRUEFER_AUSSCHUSS_ERKANNT) && countSleepAusschuss <= 4) {
				ausschuss_erkannt = JA;
			}
		} while (((val & IN_PRUEFER_AUSSCHUSS_ERKANNT) != IN_PRUEFER_AUSSCHUSS_ERKANNT) && countSleepAusschuss <= 4); // 4=200ms //

		if (writeOnModBus(OUT_PRUEFER_AUSFAHREN, RESET) == -1)
			goto fail;

    // Hier erfolgt die Auswertung, ob es sich um ein Ausschussteil handelt.
    // In Abhängigkeit davon wird ein entsprechender Mailinhalt zurückgesendet.
		if (ausschuss_erkannt == JA) {
			rt_printk("Ausschuss erkannt\n");
			letter_Pruefer = AUSCHUSS;
			rt_sleep(100 * nano2count(1000000)); // Prüfer fährt sicher wieder hoch
			ausschuss_erkannt = NEIN;
			rt_mbx_send(&mbox[mailBoxControl], &letter_Pruefer, sizeof(letter_Pruefer));		//Ausschuss erkannt
		} else {
			letter_Pruefer = MB_PRUEFER;
			rt_sleep(100 * nano2count(1000000));	// Prüfer fährt sicher wieder hoch
			rt_mbx_send(&mbox[mailBoxControl], &letter_Pruefer, sizeof(letter_Pruefer));		//Bin fertig!
		}
		rt_printk("taskTwo received message in Funktion::::: %d\n",letter_Pruefer);
	}
  // Wenn Fehler auftreten
	fail: rt_printk("puefer: Modus Fehler\n");
	rt_modbus_disconnect(fd_node);
	rt_printk("puefer: MODBUS communication failed\n");
	rt_printk("puefer: task exited\n");

	rt_task_delete(&taskDrehteller);
	rt_task_delete(&taskAuswerfer);
	rt_task_delete(&taskBohrmaschine);
	rt_task_delete(&taskControl);

	for (cnt_Mail_Delete = mailBoxAuswerfer; cnt_Mail_Delete < lastMailBox; cnt_Mail_Delete++)
		rt_mbx_delete(&mbox[cnt_Mail_Delete]);

	rt_sem_delete(&sem);

	rt_printk("rtai_example unloaded\n");
	rt_printk("Sie muessen das Programm neu starten.\n");
}

// Die Bohrmaschine und der Feststeller werden nur aktiviert, wenn ein Bauteil vorhanden ist und wenn es kein Ausschussteil ist.
static void bohrmaschine(long x) {
  // lokale Variablen für Sensorwerte, Mailboxen löschen und Mailboxgröße
  static int val = 0;
  int cnt_Mail_Delete;
	uint8_t letter_Bohrer;
 // uint8_t bohrer_verzoegert_einschalten = 0;

	while (1) {
		rt_mbx_receive(&mbox[mailBoxBohrmaschine], &letter_Bohrer, sizeof(letter_Bohrer));		//Empfange Nachricht

    // Fahre den Bohrer nach oben, wenn er noch nicht ganz oben ist. -> Nur zur Sicherheit!
		if (writeOnModBus(OUT_BOHRER_HOCHFAHREN, SET) == -1)
			goto fail;
		do{
			rt_sleep(50 * nano2count(1000000));
			if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
				goto fail;
		}while((val & IN_BOHRER_OBEN) != IN_BOHRER_OBEN);

		if (writeOnModBus(OUT_BOHRER_HOCHFAHREN, RESET) == -1)
			goto fail;

			// Fahre jetzt den Bohrer nach unten...
			if (writeOnModBus(OUT_BOHRER_RUNTERFAHREN, SET) == -1)
				goto fail;
			// und spanne jetzt das Werkstück.
			if (writeOnModBus(OUT_WERSTUECK_FESTHALTEN, SET) == -1)
				goto fail;
			rt_printk("Spanne Werkstueck\n");

				// schalte Bohrer bereits beim runterfahren ein.
				rt_printk("Bohrer einschalten\n");
				if (writeOnModBus(OUT_BOHRER, SET) == -1)
					goto fail;
			do{
				rt_sleep(50 * nano2count(1000000));
				if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
					goto fail;
			}while((val & IN_BOHRER_UNTEN) != IN_BOHRER_UNTEN);

			if (writeOnModBus(OUT_BOHRER_RUNTERFAHREN, RESET) == -1)
				goto fail;
			// Bohre für 500ms.
			rt_sleep(300 * nano2count(1000000));
			if (writeOnModBus(OUT_BOHRER_HOCHFAHREN, SET) == -1)
				goto fail;
			do{
				rt_sleep(50 * nano2count(1000000));
				if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
					goto fail;
			}while((val & IN_BOHRER_OBEN) != IN_BOHRER_OBEN);

			if (writeOnModBus(OUT_BOHRER_HOCHFAHREN, RESET) == -1)
				goto fail;
			// Bohrer ausschalten
			if (writeOnModBus(OUT_BOHRER, RESET) == -1)
				goto fail;
			rt_printk("Bohrer ausschalten\n");

			if (writeOnModBus(OUT_WERSTUECK_FESTHALTEN, RESET) == -1)
				goto fail;
			rt_printk("Werkstueck freigeben\n");


			letter_Bohrer = MB_BOHRER;
		rt_mbx_send(&mbox[mailBoxControl], &letter_Bohrer, sizeof(letter_Bohrer));		//Bin fertig!

	}
  // Fehlerfall
	fail: rt_printk("bohrer: Modus Fehler\n");
	rt_modbus_disconnect(fd_node);
	rt_printk("bohrer: MODBUS communication failed\n");
	rt_printk("bohrer: task exited\n");

	rt_task_delete(&taskDrehteller);
	rt_task_delete(&taskAuswerfer);
	rt_task_delete(&taskPruefer);
	rt_task_delete(&taskControl);

	for (cnt_Mail_Delete = mailBoxAuswerfer; cnt_Mail_Delete < lastMailBox; cnt_Mail_Delete++)
		rt_mbx_delete(&mbox[cnt_Mail_Delete]);

	rt_sem_delete(&sem);

	rt_printk("rtai_example unloaded\n");
	rt_printk("Sie muessen das Programm neu starten.\n");
}

// Der Drehsteller dreht sich nur, wenn die Sensoren ein Bauteil detektieren
static void drehteller(long x) {
// lokale Variablen für Sensorwerte, Mailboxen löschen und Mailboxgröße
	static int val = 0;
	int cnt_Mail_Delete;
  uint8_t letter_Drehteller;

	while (1) {
		rt_mbx_receive(&mbox[mailBoxDrehteller], &letter_Drehteller, sizeof(letter_Drehteller)); //Startet erst, wenn Mail im Postfach vorhanden
    // starte Drehteller
		if (writeOnModBus(OUT_DREHTELLER, SET) == -1)
			goto fail;

    // Überprüfe, ob Drehteller seine Position bereits verlassen hat.
		do {
			rt_sleep(50 * nano2count(1000000));
			if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
				goto fail;
		} while ((val & IN_DREHTELLER_IN_POSITION) == IN_DREHTELLER_IN_POSITION);

		if (writeOnModBus(OUT_DREHTELLER, RESET) == -1)
			goto fail;

		do {
			rt_sleep(50 * nano2count(1000000));
			if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
				goto fail;
		} while ((val & IN_DREHTELLER_IN_POSITION) != IN_DREHTELLER_IN_POSITION);

		rt_sleep(100 * nano2count(1000000)); //zum Erreichen der Endposition
		letter_Drehteller = MB_DREHTELLER;
		rt_mbx_send(&mbox[mailBoxControl], &letter_Drehteller, sizeof(letter_Drehteller));		//Bin fertig!
	}
  // Fehlerfall
	fail: rt_printk("drehteller: Modus Fehler\n");
	rt_modbus_disconnect(fd_node);
	rt_printk("drehteller: MODBUS communication failed\n");
	rt_printk("drehteller: task exited\n");

	rt_task_delete(&taskPruefer);
	rt_task_delete(&taskAuswerfer);
	rt_task_delete(&taskBohrmaschine);
	rt_task_delete(&taskControl);

	for (cnt_Mail_Delete = mailBoxAuswerfer; cnt_Mail_Delete < lastMailBox; cnt_Mail_Delete++)
		rt_mbx_delete(&mbox[cnt_Mail_Delete]);

	rt_sem_delete(&sem);

	rt_printk("rtai_example unloaded\n");
	rt_printk("Sie muessen das Programm neu starten.\n");
}

/* Sicheres schreiben der Ausgänge; */

/* Diese Funktion *muss* exklusiv ablaufen, darf also nicht durch andere Tasks
 * unterbrochen werden! Diese Exklusivitaet wird ueber ein Semaphor realisiert.
 * Problem: Blockiert in read/writeAllBits die MODBUS-Funktion, wird durch die
 * Taskumschaltung u.U. ein bereits gelesener Wert manipuliert und spaeter mit
 * dem falschen Wert weitergerechnet. Diese Funktion ist damit nicht reentrant!
 * Die Werte der unterbrechenden Tasks wuerden somit nicht wirksam werden... :-(
 */
static int writeOnModBus(uint8_t mask, uint8_t mode) {
	unsigned short val;
  // Sperre Ressource
	if (rt_sem_wait(&sem) == 0xffff)
		return -1;

	if (rt_modbus_get(fd_node, DIGITAL_OUT, 0, &val))
		return -1;
#ifdef TEST
	rt_printk("Wert vorher: 0x%llx\n", val);
#endif
	if (mode == SET)
		val |= mask;
	else if (mode == RESET)
		val &= ~mask;
#ifdef TEST
	rt_printk("Wert nachher: 0x%llx\n", val);
#endif
	if (rt_modbus_set(fd_node, DIGITAL_OUT, 0, val))
		return -1;

	/* Weiteres Problem: Wird nach dem Schreiben der Ausgaenge der Zustand der
	 * Ausgaenge eher wieder eingelesen, als die Ausgaenge physikalisch aktiv
	 * sind, werden falsche Werte gelesen. Deshalb muss an dieser Stelle
	 * gewartet werden, bis die Ausgaenge stabil sind. Wie lange? Hmm, die
	 * Modbus-Doku sagt nix, also ausprobieren!
	 */

	rt_sleep(1 * nano2count(1000000));
  // Gebe Ressource wieder frei
	if (rt_sem_signal(&sem) == 0xffff)
		return -1;

	return 0;

	/* Weiteres Problem: Wird nach dem Schreiben der Ausgaenge der Zustand der
	 * Ausgaenge eher wieder eingelesen, als die Ausgaenge physikalisch aktiv
	 * sind, werden falsche Werte gelesen. Deshalb muss an dieser Stelle
	 * gewartet werden, bis die Ausgaenge stabil sind. Wie lange? Hmm, die
	 * Modbus-Doku sagt nix, also ausprobieren!
	 */

	rt_sleep(1 * nano2count(1000000));

	if (rt_sem_signal(&sem) == 0xffff)
		return -1;

	return 0;
}

static int init_Aktoren(int fd_node) {
  // lokale Variablen für die eingelesenen Sensorwerte
  // und für die Größe der Mails
	uint8_t val;
	uint8_t letter_Drehteller;
	uint8_t letter_Auswerfer;
	uint8_t zuletztGebohrt;

	// Bohrer hochfahren
	if (writeOnModBus(OUT_BOHRER_HOCHFAHREN, SET) == -1)
				return -1;
			do{
				rt_sleep(50 * nano2count(1000000));
				if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
					return -1;
			}while((val & IN_BOHRER_OBEN) != IN_BOHRER_OBEN);

	//Drehteller leerfahren
	while ((((val & IN_WERKSTUECK_IM_DREHTELLER) == IN_WERKSTUECK_IM_DREHTELLER) | ((val & IN_WERSTUEK_IN_MESSVORRICHTUNG)
			== IN_WERSTUEK_IN_MESSVORRICHTUNG) | ((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG) == IN_WERSTUEK_IN_BOHRVORRICHTUNG))) {
		zuletztGebohrt = NEIN;

		/* Einlesen der Eingänge*/
		if (rt_modbus_get(fd_node, DIGITAL_IN, 0, (unsigned short *) &val))
			return -1;

		if ((val & IN_WERSTUEK_IN_BOHRVORRICHTUNG)== IN_WERSTUEK_IN_BOHRVORRICHTUNG) {
			zuletztGebohrt = JA;
		}

    // Drehteller dreht einmal weiter
		rt_mbx_send(&mbox[mailBoxDrehteller], &letter_Drehteller, sizeof(letter_Drehteller));
		rt_printk("Starte Drehteller\n");
		rt_mbx_receive(&mbox[mailBoxControl], &letter_Drehteller,	sizeof(letter_Drehteller)); //Startet erst, wenn Mail im Postfach vorhanden

		if (zuletztGebohrt == JA) {
			//Auswerfer besitzt keinen Sensor und benutzt den Sensor der Bohrvorrichtung
			rt_mbx_send(&mbox[mailBoxAuswerfer], &letter_Auswerfer, sizeof(letter_Auswerfer));
		}
		//Warte nur auf Mail, wenn der Auswerfer auch gestartet wurde
		if (zuletztGebohrt == JA) {
			rt_mbx_receive(&mbox[mailBoxControl], &letter_Auswerfer, sizeof(letter_Auswerfer)); //Warte bis Auswerfvorgang beendet wurde
		}
	}
	rt_printk("Init der Aktoren beendet.\n");

	return 0;
}

module_exit(example_exit)
module_init(example_init)
