dchat_msgp:
	gcc -o dchat_msgp dchat_msgp.c -pthread
	
dchat_traffic_control:
	gcc -o dchat_traffic_control dchat_traffic_control.c -pthread
	
dchat_encrypt:
	gcc -o dchat_enrypt encrypt_dchat.c -pthread
	
dchat_dont_decrypt:
	gcc -o dchat_dont_decrypt dont_decrypt_dchat.c -pthread
	
dchat_fair:
	gcc -o dchat_fair fair_dchat.c -pthread
	
dchat_decentralized:
	gcc -o dchat_decentralized decentralized.c -pthread
	
dchat_gui:
	gcc -o dchat_gui gtk_dchat.c `pkg-config --cflags --libs gtk+-2.0` -pthread

clean:
	if [ -e dchat_msgp ] ; \
	then \
	     rm dchat_msgp ; \
	fi;
	if [ -e dchat_traffic_control ] ; \
	then \
	     rm dchat_traffic_control ; \
	fi;
	if [ -e dchat_encrypt ] ; \
	then \
	     rm dchat_encrypt ; \
	fi;
	if [ -e dchat_dont_decrypt ] ; \
	then \
	     rm dchat_dont_decrypt ; \
	fi;
	if [ -e dchat_decentralized ] ; \
	then \
	     rm dchat_decentralized ; \
	fi;
	if [ -e dchat_gui ] ; \
	then \
	     rm dchat_gui ; \
	fi;
	if [ -e dchat_fair ] ; \
	then \
	     rm dchat_fair ; \
	fi;