// *******************************************************
// Version ..... : V1.0 du 27/05/2024
// 
// Fonctionnalités :
// 		- les pipes
//
// ********************************************************/

# include "sys.h"
#include <string.h>

int decouper(char *, char *, char *[], int);
int exec_pipeline(char *);
int lance_cmd(char *[]);
int traite_pipe(int, char *);

enum {
	MaxLigne = 1024, 			// longueur max d'une ligne de commandes
	MaxMot = MaxLigne / 2, 		// nbre max de mot dans la ligne
	MaxDirs = 100, 				// nbre max de repertoire dans PATH
	MaxPathLength = 512, 		// longueur max d'un nom de fichier
	MaxPipes = 128,				// nbre max de commandes séparées par " | " sur une ligne
};

# define PROMPT "V1.0 "
char * path[MaxDirs];			// liste des répertoires de PATH (globale ???)

int main(int argc, char * argv[]){
	
	char ligne[MaxLigne];			// la dernière ligne entrée par l'utilisateur

	/* Decouper UNE COPIE de PATH en repertoires */
	decouper(strdup(getenv("PATH")), ":", path, MaxDirs);

	/* Lire et traiter chaque ligne de commande */
	for (printf(PROMPT); fgets(ligne, sizeof ligne, stdin) != 0; printf(PROMPT))
		exec_pipeline(ligne);

	printf("Bye\n");
	return 0;
}

/* Exécute une pipeline (une série de commandes séparées par des | */
int exec_pipeline(char * pipeline){
	char * mot[MaxMot];				// liste des mots de la ligne saisie
	char * liste_cmd[MaxPipes];		// la ou les (en cas de pipe(s)) commande(s) de la ligne
	int tmp, i;
	int in = 0;						// entrée à utiliser pour le prochain processus

	/* Séparer les différentes commandes en cas de pipe */
		decouper(pipeline, "|\n", liste_cmd, MaxPipes);	
		if (liste_cmd[0] == 0) return 0;				// ligne vide

		/* Traiter chaque commande de la ligne, sauf la dernière */
		for (i = 0; liste_cmd[i+1] != 0; i++) {	
			/* Exécuter une commande et récupérer le fichier qui servira d'entrée pour la suivante */
			if ((in = traite_pipe(in, liste_cmd[i])) < 0) {
				fprintf(stderr, "Problème dans l'exécution de la commande : %s.\n", liste_cmd[i]);
				return -1; }
		}

		/* lancement du processus final de la ligne de commande */
		if ((tmp = fork()) < 0){
			perror("fork");
			return -1;}

		// parent : attendre la fin de l'enfant
		if (tmp != 0){
			while(wait(0) != tmp);
			return 0;}

		// enfant : exec du programme
		if(in != 0) {
			dup2(in, 0);		// récupère en entrée la sortie du dernier pipe
			close(in); }
		
		decouper(liste_cmd[i], " \t\n", mot, MaxMot);
		lance_cmd(mot);	// exécute la dernière commande de la ligne
		return 0;
}

/* 	Lance un processus enfant qui lit depuis l'entrée spécifiée (in) et écrit dans un pipe
	Renvoie le descripteur de fichier correspondant à la sortie du tube */
int traite_pipe(int in, char * commande){
	int pipe_fd[2];				
	int cmd;	
	char * mot[MaxMot];

	if (decouper(commande, " \t\n", mot, MaxMot) == 0)	// commande vide
	// if (strcmp(commande, " \0"))
		return -1;

	/* ouvrir un pipe pour communiquer avec le processus (frère) suivant */	
	if (pipe(pipe_fd) != 0) {
		perror("pipe");
		return -1;	}

	/* créer un enfant pour exécuter la "sous-commande" */
	if ((cmd = fork()) < 0){
		perror("fork");
		return -1;}

	if (cmd == 0){				// enfant
		close(pipe_fd[0]); 		// ferme la sortie du tube (inutilisée)
		dup2(pipe_fd[1], 1); 	// redirige sa sortie sur l'entrée du tube
		close(pipe_fd[1]);
		dup2(in, 0);			// redirige la sortie utilisée précédemment sur son entrée
		close(in);
		lance_cmd(mot);	// exécute la commande
	}

	/* Parent */
	close(pipe_fd[1]);			// fermeture de l'entrée du tube actuel
	if (in != 0) close(in);		// fermeture de la sortie du tube précédent
	return(pipe_fd[0]);
}


/* exécute une commande, en cherchant l'exécutable dans la liste de répertoires fournie */
int lance_cmd(char * mot[])
{	char pathname[MaxPathLength];
	
	for (int i = 0; path[i] != 0; i ++){
		snprintf(pathname, sizeof pathname, "%s/%s", path[i], mot[0]);
		execv(pathname, mot);
	}
	fprintf(stderr, "%s: not found\n", mot[0]);
	exit(-1);
}


/* découpe une chaîne de caractères et renvoie le nombre de mots récupérés */
int decouper(char * ligne, char * separ, char * mot[], int maxmot)
{	int i ;
	mot[0] = strtok(ligne, separ);
	for(i = 1; mot[i-1] != 0; i ++){
		if (i == maxmot){
			fprintf (stderr, "Erreur dans la fonction decouper: trop de mots\n");
			mot[i-1] = 0;
			break;
		}
		mot[i] = strtok(NULL, separ);
	}
	return (i-1);
}
