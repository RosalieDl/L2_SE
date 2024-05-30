// *******************************************************
// Version ..... : V1.1 du 30/05/2024
// 
// Fonctionnalités :
// 		- les pipes
//
// ********************************************************/

# include "sys.h"
#include <string.h>
#include <ctype.h>	// pour isspace()

int decouper(char *, char *, char *[], int);
int exec_pipeline(char *);
int lance_cmd(char *);
int traite_pipe(char * []);
int est_vide(char *);

enum {
	MaxLigne = 1024, 			// longueur max d'une ligne de commandes
	MaxMot = MaxLigne / 2, 		// nbre max de mot dans la ligne
	MaxDirs = 100, 				// nbre max de repertoire dans PATH
	MaxPathLength = 512, 		// longueur max d'un nom de fichier
	MaxPipes = 128,				// nbre max de commandes séparées par " | " sur une ligne
};

# define PROMPT "V1.1 "
char * path[MaxDirs];			// liste des répertoires de PATH (globale ???)

int main(int argc, char * argv[]){
	
	char ligne[MaxLigne];			// la dernière ligne entrée par l'utilisateur
	int tmp; 

	/* Découper UNE COPIE de PATH en repertoires */
	decouper(strdup(getenv("PATH")), ":", path, MaxDirs);

	/* Lire et traiter chaque ligne saisie dans le terminal */
	for (printf(PROMPT); fgets(ligne, sizeof ligne, stdin) != 0; printf(PROMPT)){
		
		if (est_vide(ligne)){
			fprintf(stderr, "Commande vide ! (0)\n");
			continue;}		// ligne vide

		/* lancement du processus fils qui va exécuter la pipeline */
		if ((tmp = fork()) < 0){
			perror("fork");
			continue;}

		/* parent : attendre la fin de l'enfant (ou pas) */
		if (tmp != 0){
			while(wait(0) != tmp);
			/* Récupérer le statut de retour ? Faire waitpid(&status) ou un truc du genre ?*/
			continue ;} // renvoyer le bon statut

		/* enfant : prend en charge  la pipeline */
		exec_pipeline(ligne);	// pour l'instant, une seule pipeline
	}
	printf("Bye\n");
	return 0;
}

/* Exécute une pipeline (une série de commandes séparées par des "|" ) */
int exec_pipeline(char * pipeline){
	char * liste_cmd[MaxPipes];		// la ou les (en cas de pipe(s)) commande(s) de la ligne
	int i = 0;

	/* Séparer les différentes commandes en cas de pipe */
	decouper(pipeline, "|", liste_cmd, MaxPipes);

	/* plusieurs commandes successives */
	if (liste_cmd[1] != 0) {
		i = traite_pipe(liste_cmd);
		if (i < 0) {
			fprintf(stderr, "Echec de la commande [%d] : \"%s\".\n", -i, liste_cmd[-i]);
			exit(-1); }
	}

	/* dernière (ou unique) commande de la pipeline */
	lance_cmd(liste_cmd[i]);
	return 0;
}

int traite_pipe(char * commandes[]){
	int pipe_fd[2];				
	int cmd, i;
	int in = 0;

	for (i = 0; commandes[i+1] != 0; i++) {	
		if (est_vide(commandes[i])){
			fprintf(stderr, "Erreur de syntaxe. ");		// fprintf ou pas nécessaire ?
			return -i;}

		/* ouvrir un pipe pour communiquer avec le processus suivant */	
		if (pipe(pipe_fd) != 0) {
			perror("pipe");
			return -i;}

		/* créer un enfant pour exécuter la "sous-commande" */
		if ((cmd = fork()) < 0){
			perror("fork");
			return -i;}

		if (cmd == 0){				// enfant
			close(pipe_fd[0]); 		// ferme la sortie du tube (inutilisée)
			dup2(pipe_fd[1], 1); 	// redirige sa sortie sur l'entrée du tube
			close(pipe_fd[1]);
			dup2(in, 0);			// redirige la sortie utilisée précédemment sur son entrée
			close(in);
			lance_cmd(commandes[i]);	// exécute la commande
		}
		/* Parent */
		close(pipe_fd[1]);			// fermeture de l'entrée du tube actuel
		if (in != 0) close(in);		
		in = pipe_fd[0];
	}

	dup2(in, 0);		// récupère en entrée la sortie du dernier pipe
	close(in); 
	return i;
	}


/* exécute une commande, en cherchant l'exécutable dans la liste de répertoires fournie */
int lance_cmd(char * commande){	
	char * mot[MaxMot];				// liste des mots de la commande à exécuter
	char pathname[MaxPathLength];

	if (decouper(commande, " \t\n", mot, MaxMot) == 0){
		fprintf(stderr, "Erreur de syntaxe (manque la dernière commande)\n");
		exit(-1);}

	for (int i = 0; path[i] != 0; i ++){
		snprintf(pathname, sizeof pathname, "%s/%s", path[i], mot[0]);
		execv(pathname, mot);
	}
	fprintf(stderr, "%s: not found\n", mot[0]);
	exit(-1);
}


/* découpe une chaîne de caractères et renvoie le nombre de mots récupérés */
int decouper(char * ligne, char * separ, char * mot[], int maxmot){	
	int i ;
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

/* Renvoie 1 si la chaîne de caractères ne contient que des espaces, 0 sinon */
int est_vide(char *str) {
    while (*str) {
        if (!isspace(*str))
            return 0;
        str++;
    }
    return 1;
}