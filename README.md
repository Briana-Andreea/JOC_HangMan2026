# JOC_HangMan2026
Specificații
-HangMan-
1. Descrierea jocului:
	Jocul este o aplicație de tip consolă realizată în limbajul C, în care utilizatorul trebuie să ghicească un cuvânt secret literă cu literă, înainte de a epuiza numărul maxim de încercări permise.
2. Specificații de funcționalitate:
•	Alegerea cuvântului: Programul va extrage un cuvânt aleatoriu dintr-un fișier text (cuvinte.txt) sau dintr-o listă predefinită.
•	Afișarea progresului: La fiecare pas, jucătorul vede lungimea cuvântului reprezentată prin underscore-uri (ex: _ _ _ _ _).
•	Sistemul de vieți: Jucătorul are un număr fix de încercări (de regulă 6, corespunzător elementelor desenului spânzurătorii).
•	Validarea input-ului: Programul verifică dacă caracterul introdus este o literă și dacă a mai fost introdus anterior.
•	Condiții de final:
•	Victorie: Toate literele au fost ghicite.
•	Înfrângere: Numărul de vieți a ajuns la zero.
3. Specificații tehnice:
	Pentru ca jocul să fie multiplayer în rețea, avem nevoie de 2 componente care comunică prin Sockets.
•	Server-ul (C - Console): Gestionează starea jocului, verifică literele, numără greșelile și trimite actualizări către clienți.
•	Client-ul (C - GUI): Primește datele de la server, le randează grafic și trimite input-ul utilizatorului (tasta apăsată).
Tech Stack: 
•	Grafică: Raylib
•	Networking: sys/socket.h pt. Linux
•	Protocol de comunicare: TCP (asigură integritatea datelor, esențial pentru un joc turn-based)
